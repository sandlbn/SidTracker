// SID player: .sid files run in the firmware's minimal C64. A file exported
// by SIDTracker plays register for register like the tracker's own player;
// a tune whose init installs an interrupt (play address 0) plays through the
// KERNAL stub; the engine applies queued frames at the tune's rate.
#include <cstring>
#include <memory>
#include <vector>

#include "app/engine.h"
#include "c64/sid_player.h"
#include "compatibility/sid/sid_export.h"
#include "test.h"
#include "tracker/playback/player.h"

using namespace sidt;

namespace {

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    Box() {
        song->patterns = pats.get();
        song->clear();
    }
};

struct Regs : ISidSink {
    uint8_t r[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { r[chip & 1][reg & 31] = val; }
};

struct C64 {
    std::vector<uint8_t> mem = std::vector<uint8_t>(c64::SidPlayer::kRamBytes + c64::SidPlayer::kRomBytes);
    c64::SidPlayer p;
    C64() { p.setMemory(mem.data(), mem.data() + c64::SidPlayer::kRamBytes); }
};

}  // namespace

TEST(exported_song_plays_like_the_tracker) {
    Box b;
    Song& s = *b.song;
    // Two SIDs, notes on tracks 1 and 5, a non-frame tempo (CIA timer).
    s.pattern(0).cells[0][0] = {49, 1, 0, 0};
    s.pattern(0).cells[4][4] = {56, 1, 0, 0};
    s.pattern(0).cells[8][0] = {note::kKeyOff, 0, 0, 0};
    s.bpm = 100;
    std::vector<uint8_t> file(psid::kWorkBytes);
    goat::Report rep;
    const uint32_t n = psid::exportSid(s, file.data(), uint32_t(file.size()), rep);
    CHECK(n > 0);
    C64 c;
    c64::SidInfo info;
    const char* err = nullptr;
    CHECK(c.p.load(file.data(), n, info, err));
    CHECK_EQ(info.sid2, 0xD420);
    CHECK(c.p.start(1));
    CHECK(c.p.rateMilliHz() > 39950 && c.p.rateMilliHz() < 40050);   // 100 bpm = 40 ticks / s, CIA latch
    Regs mine, ref;
    Player pl;
    pl.init(&s, &ref);
    pl.playSong(0);
    int bad = 0;
    for (int f = 0; f < 400; ++f) {
        c64::SidWrite w[c64::SidPlayer::kMaxWrites];
        int k = 0;
        CHECK(c.p.frame(w, k));
        for (int i = 0; i < k; ++i) mine.r[w[i].chip][w[i].reg] = w[i].val;
        pl.tick();
        if (std::memcmp(mine.r, ref.r, sizeof mine.r)) ++bad;
    }
    CHECK_EQ(bad, 0);
}

TEST(interrupt_driven_tune_plays_through_the_kernal_stub) {
    // init: set $0314/5 to the handler; handler: INC $D418-shadow, store to
    // $D418 and $D400, JMP $EA31. Play address 0.
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 2;          // version 2
    f[7] = 0x7C;       // data offset
    f[10] = 0x10;      // init $1000
    f[15] = 1;         // 1 song
    f[17] = 1;
    const uint8_t prg[] = {
        0x00, 0x10,                    // load $1000
        // $1000 init: LDA #<irq STA $0314 LDA #>irq STA $0315 RTS
        0xA9, 0x0B, 0x8D, 0x14, 0x03, 0xA9, 0x10, 0x8D, 0x15, 0x03, 0x60,
        // $100B irq: INC $10F0 LDA $10F0 STA $D418 STA $D400 JMP $EA31
        0xEE, 0xF0, 0x10, 0xAD, 0xF0, 0x10, 0x8D, 0x18, 0xD4, 0x8D, 0x00, 0xD4, 0x4C, 0x31, 0xEA};
    f.insert(f.end(), prg, prg + sizeof prg);
    C64 c;
    c64::SidInfo info;
    const char* err = nullptr;
    CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
    CHECK(c.p.start(1));
    for (int fr = 1; fr <= 5; ++fr) {
        c64::SidWrite w[c64::SidPlayer::kMaxWrites];
        int k = 0;
        CHECK(c.p.frame(w, k));
        CHECK_EQ(k, 2);
        CHECK_EQ(w[0].reg, 0x18);
        CHECK_EQ(w[0].val, fr);
    }
    CHECK_EQ(c.p.rateMilliHz() / 1000, 50u);   // VBI, PAL
}

TEST(rsid_and_garbage_are_refused) {
    C64 c;
    c64::SidInfo info;
    const char* err = nullptr;
    std::vector<uint8_t> f(0x80, 0);
    std::memcpy(f.data(), "RSID", 4);
    CHECK(!c.p.load(f.data(), uint32_t(f.size()), info, err));
    CHECK(err && std::strstr(err, "RSID"));
    std::memcpy(f.data(), "JUNK", 4);
    CHECK(!c.p.load(f.data(), uint32_t(f.size()), info, err));
}

TEST(engine_applies_one_frame_per_tick) {
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    song->clear();
    std::unique_ptr<Engine> e(new Engine);
    e->init(song.get(), 44100000);
    struct Tap : IStreamTap {
        std::vector<uint64_t> at;
        void onWrite(uint64_t sample, uint8_t, uint8_t, uint8_t) override { at.push_back(sample); }
    } tap;
    e->setTap(&tap);
    e->startExternal(50000);   // 50 Hz: 882 samples a tick
    for (int k = 0; k < 4; ++k) {
        const c64::SidWrite w = {0, 0x18, uint8_t(k)};
        CHECK(e->pushFrame(&w, 1));
    }
    std::vector<int16_t> out(256);
    for (int blk = 0; blk < 30; ++blk) e->fill(out.data(), 128);   // 3840 samples
    CHECK_EQ(tap.at.size(), 4u);
    if (tap.at.size() == 4) {
        CHECK_EQ(tap.at[1] - tap.at[0], 882u);
        CHECK_EQ(tap.at[3] - tap.at[2], 882u);
    }
    CHECK_EQ(e->framesQueued(), 0u);
    e->stopExternal();
    CHECK(!e->external());
}

TEST(frames_of_an_earlier_tune_are_dropped) {
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    song->clear();
    std::unique_ptr<Engine> e(new Engine);
    e->init(song.get(), 44100000);
    struct Tap : IStreamTap {
        std::vector<uint8_t> vals;
        void onWrite(uint64_t, uint8_t, uint8_t, uint8_t v) override { vals.push_back(v); }
    } tap;
    e->setTap(&tap);
    e->startExternal(50000);
    const c64::SidWrite old = {0, 0x18, 0xAA};
    CHECK(e->pushFrame(&old, 1));
    e->stopExternal();          // the audio side has not run in between
    e->startExternal(50000);
    const c64::SidWrite now = {0, 0x18, 0x01};
    CHECK(e->pushFrame(&now, 1));
    std::vector<int16_t> out(256);
    for (int blk = 0; blk < 10; ++blk) e->fill(out.data(), 128);
    CHECK_EQ(tap.vals.size(), 1u);
    if (!tap.vals.empty()) CHECK_EQ(tap.vals[0], 0x01);
}

TEST(undocumented_opcodes) {
    // LAX #imm via LXA, SAX, DCP, ISC, SLO, SBX on known values.
    std::vector<uint8_t> ram(65536, 0);
    struct NoBus : c64::Bus {
        uint8_t read(uint16_t) override { return 0x60; }
        void write(uint16_t, uint8_t) override {}
    } bus;
    c64::Mos6502 cpu;
    cpu.ram = ram.data();
    cpu.bus = &bus;
    const uint8_t prog[] = {
        0xA9, 0xF0,        // LDA #$F0
        0xA2, 0x3C,        // LDX #$3C
        0x87, 0x10,        // SAX $10      -> $30
        0xA9, 0x05,        // LDA #$05
        0x85, 0x11,        // STA $11
        0xC7, 0x11,        // DCP $11      -> $04, CMP A=$05: C set
        0xE7, 0x12,        // ISC $12      -> $01, A = 5 - 1 = 4 (carry from DCP)
        0xA9, 0x81, 0x85, 0x13,
        0x07, 0x13,        // SLO $13      -> $02, A = 4 | 2 = 6
        0xA2, 0x0F,        // LDX #$0F
        0xCB, 0x02,        // SBX #$02     X = (6 & $0F) - 2 = 4
        0xA7, 0x10,        // LAX $10      A = X = $30
        0x60};
    std::memcpy(&ram[0x1000], prog, sizeof prog);
    CHECK(cpu.call(0x1000, 0));
    CHECK_EQ(ram[0x10], 0x30);
    CHECK_EQ(ram[0x11], 0x04);
    CHECK_EQ(ram[0x12], 0x01);
    CHECK_EQ(ram[0x13], 0x02);
    CHECK_EQ(cpu.a, 0x30);
    CHECK_EQ(cpu.x, 0x30);
}

TEST(init_writes_are_kept) {
    // Commando-style: the volume is set once, in init.
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 2;
    f[7] = 0x7C;
    f[10] = 0x10;              // init $1000
    f[12] = 0x10, f[13] = 0x06; // play $1006
    f[15] = 1;
    f[17] = 1;
    const uint8_t prg[] = {0x00, 0x10,
                           0xA9, 0x0F, 0x8D, 0x18, 0xD4, 0x60,   // init: LDA #$0F STA $D418 RTS
                           0x60};                                // play: RTS
    f.insert(f.end(), prg, prg + sizeof prg);
    C64 c;
    c64::SidInfo info;
    const char* err = nullptr;
    CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
    c64::SidWrite w[c64::SidPlayer::kMaxWrites];
    int n = 0;
    CHECK(c.p.start(1, w, &n));
    CHECK_EQ(n, 1);
    CHECK_EQ(w[0].reg, 0x18);
    CHECK_EQ(w[0].val, 0x0F);
}

namespace {
// A PSID v4 around `code` loaded at $1000 (init $1000, play `play`).
std::vector<uint8_t> makePsid(const std::vector<uint8_t>& code, uint16_t play, bool ntsc = false, bool cia = false,
                          uint8_t sid3 = 0) {
    std::vector<uint8_t> f(0x7C, 0);
    std::memcpy(f.data(), "PSID", 4);
    f[5] = 4;
    f[7] = 0x7C;
    f[10] = 0x10;
    f[12] = uint8_t(play >> 8);
    f[13] = uint8_t(play);
    f[15] = 1;
    f[17] = 1;
    f[0x15] = cia ? 1 : 0;
    f[0x77] = uint8_t((ntsc ? 2 : 1) << 2);
    f[0x7B] = sid3;
    f.push_back(0x00);
    f.push_back(0x10);
    f.insert(f.end(), code.begin(), code.end());
    return f;
}
}  // namespace

TEST(fable_review_fixes) {
    c64::SidInfo info;
    const char* err = nullptr;
    c64::SidWrite w[c64::SidPlayer::kMaxWrites];
    int n = 0;
    {   // $02A6: PAL = 1, NTSC = 0. init copies it to $D418.
        const std::vector<uint8_t> code = {0xAD, 0xA6, 0x02, 0x8D, 0x18, 0xD4, 0x60};
        for (int nt = 0; nt < 2; ++nt) {
            C64 c;
            const auto f = makePsid(code, 0x1006, nt);
            CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
            CHECK(c.p.start(1, w, &n));
            CHECK(n == 1 && w[0].val == (nt ? 0 : 1));
        }
    }
    {   // NTSC + CIA without a timer write: 60 Hz, not 62.3 Hz.
        C64 c;
        const auto f = makePsid({0x60}, 0x1000, true, true);
        CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
        CHECK(c.p.start(1));
        CHECK(c.p.rateMilliHz() > 59900 && c.p.rateMilliHz() < 60100);
    }
    {   // A third SID at $D440: its writes do not reach SID 1.
        C64 c;
        const auto f = makePsid({0x8D, 0x58, 0xD4, 0x8D, 0x18, 0xD4, 0x60}, 0x1006, false, false, 0x44);
        CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
        CHECK_EQ(info.sid3, 0xD440);
        CHECK(c.p.start(1, w, &n));
        CHECK_EQ(n, 1);   // only the $D418 write
    }
    {   // init polls $D011 bit 7: the loop ends.
        C64 c;
        const auto f = makePsid({0x2C, 0x11, 0xD0, 0x10, 0xFB, 0x60}, 0x1005);   // BIT $D011 / BPL -5 / RTS
        CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
        CHECK(c.p.start(1));
    }
    {   // An IRQ handler ending in JMP $EA7E returns.
        C64 c;
        const auto f = makePsid({0xA9, 0x0B, 0x8D, 0x14, 0x03, 0xA9, 0x10, 0x8D, 0x15, 0x03, 0x60,   // init: ($0314) = $100B
                             0x8D, 0x18, 0xD4, 0x4C, 0x7E, 0xEA},                               // irq: STA $D418, JMP $EA7E
                            0);
        CHECK(c.p.load(f.data(), uint32_t(f.size()), info, err));
        CHECK(c.p.start(1));
        for (int fr = 0; fr < 3; ++fr) CHECK(c.p.frame(w, n));
    }
    {   // SHX abs,Y: X & (H + 1).
        std::vector<uint8_t> ram(65536, 0);
        struct NoBus : c64::Bus {
            uint8_t read(uint16_t) override { return 0x60; }
            void write(uint16_t, uint8_t) override {}
        } bus;
        c64::Mos6502 cpu;
        cpu.ram = ram.data();
        cpu.bus = &bus;
        const uint8_t prog[] = {0xA2, 0xFF, 0xA0, 0x02, 0x9E, 0x00, 0x20, 0x60};   // LDX #$FF LDY #2 SHX $2000,Y
        std::memcpy(&ram[0x1000], prog, sizeof prog);
        CHECK(cpu.call(0x1000, 0));
        CHECK_EQ(ram[0x2002], 0x21);
    }
}
