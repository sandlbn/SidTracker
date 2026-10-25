// MIDI: input parser, ASID encoder (the documented register order), engine
// clock / ASID output per tick, and the app's note, CC and clock-sync input.
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "app/engine.h"
#include "app/midi.h"
#include "test.h"

using namespace sidt;

namespace {

struct Capture final : ot::IMidiPort {
    std::vector<uint8_t> out[2];
    std::deque<uint8_t> in[2];
    bool send(ot::MidiPortId port, const uint8_t* d, uint32_t n) override {
        out[int(port)].insert(out[int(port)].end(), d, d + n);
        return true;
    }
    uint32_t receive(ot::MidiPortId port, uint8_t* dst, uint32_t cap) override {
        uint32_t n = 0;
        auto& q = in[int(port)];
        while (n < cap && !q.empty()) {
            dst[n++] = q.front();
            q.pop_front();
        }
        return n;
    }
};

struct Rig {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    std::unique_ptr<Engine> e{new Engine};
    Capture midi;
    std::vector<int16_t> out = std::vector<int16_t>(256);
    Rig() {
        song->patterns = pats.get();
        song->clear();
        e->init(song.get(), 44100000);
        e->setMidiOut(&midi);
    }
    void blocks(int n) {
        for (int i = 0; i < n; ++i) e->fill(out.data(), 128);
    }
};

int count(const std::vector<uint8_t>& v, uint8_t b) {
    int n = 0;
    for (uint8_t x : v) n += x == b;
    return n;
}

}  // namespace

TEST(parser_running_status_realtime_and_sysex) {
    midi::Parser p;
    midi::Parser::Msg m;
    std::vector<midi::Parser::Msg> got;
    const uint8_t bytes[] = {0x90, 60, 0xF8, 100,   // clock inside a note-on
                             62, 90,                // running status
                             0xF0, 1, 2, 3, 0xF7,   // SysEx skipped
                             0xC1, 5,               // program change: one data byte
                             0xB0, 74, 127};
    for (uint8_t b : bytes)
        if (p.feed(b, m)) got.push_back(m);
    CHECK_EQ(got.size(), 5u);
    if (got.size() == 5) {
        CHECK_EQ(got[0].status, 0xF8);
        CHECK(got[1].status == 0x90 && got[1].d1 == 60 && got[1].d2 == 100);
        CHECK(got[2].status == 0x90 && got[2].d1 == 62 && got[2].d2 == 90);
        CHECK(got[3].status == 0xC1 && got[3].d1 == 5);
        CHECK(got[4].status == 0xB0 && got[4].d1 == 74 && got[4].d2 == 127);
    }
}

TEST(asid_frame_layout) {
    // Bit order: registers 0-3, 5-10, 12-17, 19-24, then 4, 11, 18.
    const int order[25] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 4, 11, 18};
    midi::AsidEncoder a;
    for (int r = 0; r < 25; ++r) a.write(0, uint8_t(r), uint8_t(0x80 | r));   // every register, bit 7 set
    a.write(0, 4, 0x41);    // a second write to control 1: the retrigger slot (bit 25)
    a.write(1, 0x18, 0x0F); // SID 2, volume
    uint8_t buf[2 * midi::AsidEncoder::kMaxMessage];
    const uint32_t n = a.flush(buf, sizeof buf);
    CHECK_EQ(buf[0], 0xF0);
    CHECK_EQ(buf[1], 0x2D);
    CHECK_EQ(buf[2], 0x4E);
    // Masks: bits 0-25 set.
    const uint32_t mask = buf[3] | (buf[4] << 7) | (buf[5] << 14) | (uint32_t(buf[6]) << 21);
    CHECK_EQ(mask, (1u << 26) - 1);
    const uint32_t hi = buf[7] | (buf[8] << 7) | (buf[9] << 14) | (uint32_t(buf[10]) << 21);
    CHECK_EQ(hi, (1u << 25) - 1);   // the retrigger write $41 has bit 7 clear
    for (int b = 0; b < 25; ++b) CHECK_EQ(buf[11 + b], order[b]);   // data = register number (low 7 bits)
    CHECK_EQ(buf[11 + 25], 0x41);
    CHECK_EQ(buf[11 + 26], 0xF7);
    const uint32_t second = 11 + 27;
    CHECK_EQ(buf[second + 2], 0x50);   // SID 2
    CHECK_EQ(buf[second + 3 + 21 / 7], uint8_t(1u << (21 % 7)));   // register $18 = bit 21
    CHECK_EQ(buf[n - 2], 0x0F);
    CHECK_EQ(buf[n - 1], 0xF7);
    CHECK(!a.pending());
}

TEST(engine_clock_six_per_row_and_start_stop) {
    for (uint8_t speed : {uint8_t(6), uint8_t(3), uint8_t(4)}) {
        Rig r;
        r.song->speed = speed;
        r.e->setClockPorts(1);   // DIN
        r.e->playPattern(0);
        // Ticks at 125 bpm: 882 samples; 16 rows = 16 * speed ticks.
        const int ticks = 16 * speed;
        r.blocks((ticks * 882 + 64) / 128);
        auto& o = r.midi.out[0];
        CHECK_EQ(o.empty() ? 0 : o[0], 0xFA);
        const int clocks = count(o, 0xF8);
        CHECK(clocks >= 16 * 6 - 6 && clocks <= 16 * 6 + 6);   // 6 a row
        r.e->stop();
        r.blocks(10);
        CHECK_EQ(count(r.midi.out[0], 0xFC), 1);
        CHECK(r.midi.out[1].empty());   // USB not selected
    }
}

TEST(engine_asid_one_frame_per_tick_on_usb) {
    Rig r;
    r.song->pattern(0).cells[0][0] = {49, 1, 0, 0};
    r.e->setAsidPorts(2);   // USB
    r.e->playPattern(0);
    r.blocks(100);          // about 14 ticks
    auto& o = r.midi.out[1];
    CHECK(o.size() > 20);
    CHECK(o[0] == 0xF0 && o[1] == 0x2D && o[2] == 0x4C);   // ASID start first
    CHECK(count(o, 0xF7) >= 5);
    CHECK(r.midi.out[0].empty());
    r.e->setAsidPorts(0);
    r.blocks(2);
    CHECK(o[o.size() - 2] == 0x4D);   // stop
}

namespace {
struct Refuser final : ot::IMidiPort {
    std::vector<uint8_t> out;
    int refuse = 0;   // refuse the next n sends
    bool send(ot::MidiPortId, const uint8_t* d, uint32_t n) override {
        if (refuse > 0) {
            --refuse;
            return false;
        }
        out.insert(out.end(), d, d + n);
        return true;
    }
    uint32_t receive(ot::MidiPortId, uint8_t*, uint32_t) override { return 0; }
};
// Registers (SID 1) carried by the ASID frames in `o` (masks decoded).
uint32_t sid1Registers(const std::vector<uint8_t>& o) {
    const int order[28] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 4, 11, 18, 4, 11, 18};
    uint32_t regs = 0;
    for (size_t i = 0; i + 11 < o.size(); ++i)
        if (o[i] == 0xF0 && o[i + 1] == 0x2D && o[i + 2] == 0x4E) {
            const uint32_t mask = o[i + 3] | (o[i + 4] << 7) | (o[i + 5] << 14) | (uint32_t(o[i + 6]) << 21);
            for (int b = 0; b < 28; ++b)
                if ((mask >> b) & 1) regs |= 1u << order[b];
        }
    return regs;
}
}  // namespace

TEST(asid_start_sends_the_register_image) {
    // Fable review: registers the player wrote before ASID was switched on
    // (volume $18) never reached the receiver.
    Rig r;
    r.song->pattern(0).cells[0][0] = {49, 1, 0, 0};
    r.e->playPattern(0);
    r.blocks(40);            // the volume is written in the first tick
    r.e->setAsidPorts(1);
    r.blocks(10);
    CHECK((sid1Registers(r.midi.out[0]) >> 0x18) & 1);
    CHECK((sid1Registers(r.midi.out[0]) & 0x1FFFFFF) == 0x1FFFFFF);   // all 25
}

TEST(asid_refused_frame_is_sent_later) {
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    song->clear();
    std::unique_ptr<Engine> e(new Engine);
    e->init(song.get(), 44100000);
    Refuser port;
    e->setMidiOut(&port);
    e->setAsidPorts(1);
    std::vector<int16_t> out(256);
    for (int i = 0; i < 10; ++i) e->fill(out.data(), 128);   // start + image sent
    port.out.clear();
    port.refuse = 1;                                          // the next frame is refused
    e->noteOn(0, 49, 1);
    for (int i = 0; i < 20; ++i) e->fill(out.data(), 128);
    CHECK((sid1Registers(port.out) >> 4) & 1);   // the gate write still arrives
    CHECK((sid1Registers(port.out) >> 0) & 1);   // and the frequency
}

TEST(clock_port_change_while_playing_sends_stop_and_start) {
    Rig r;
    r.e->setClockPorts(1);
    r.e->playPattern(0);
    r.blocks(20);
    r.e->setClockPorts(2);   // DIN -> USB while playing
    r.blocks(20);
    CHECK_EQ(count(r.midi.out[0], 0xFC), 1);   // DIN got its Stop
    CHECK(!r.midi.out[1].empty() && r.midi.out[1][0] == 0xFA);   // USB starts with Start
}

TEST(asid_write_after_the_register_image_replaces_it) {
    // Found on hardware 2026-10-19: the image sent on ASID start and the
    // player's control writes in the same tick went out as a retrigger.
    midi::AsidEncoder a;
    for (int r = 0; r < 25; ++r) a.set(0, uint8_t(r), 0);
    a.write(0, 4, 0x41);   // the tick's real control write
    uint8_t buf[midi::AsidEncoder::kMaxMessage];
    uint32_t taken = 0;
    CHECK(a.build(0, buf, taken) > 0);
    CHECK_EQ(taken, (1u << 25) - 1);   // no retrigger slot
    CHECK_EQ(buf[11 + 22], 0x41);      // control 1 carries the new value
    // From the next frame on, a second write is a retrigger again.
    a.write(0, 4, 0x40);
    a.write(0, 4, 0x41);
    CHECK(a.build(0, buf, taken) > 0);
    CHECK_EQ(taken, (1u << 22) | (1u << 25));
}

// Fable review 2026-10-22: a refused image frame lost the "image" marks, so
// the next control write became a retrigger again.
TEST(asid_refused_image_stays_replaceable) {
    midi::AsidEncoder a;
    for (int r = 0; r < 25; ++r) a.set(0, uint8_t(r), 0x41);
    uint8_t buf[midi::AsidEncoder::kMaxMessage];
    uint32_t taken = 0;
    CHECK(a.build(0, buf, taken) > 0);
    a.restore(0, taken);   // the port refused it
    a.write(0, 4, 0x40);
    CHECK(a.build(0, buf, taken) > 0);
    CHECK_EQ(taken, (1u << 25) - 1);   // no retrigger slot
    CHECK_EQ(buf[11 + 22], 0x40);
}
