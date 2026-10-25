// .sid export tests (M9): the exported file's 6502 play routine, run on the
// oracle's 6502 emulator, must leave both SIDs' registers exactly as
// SIDTracker's player does after every frame -- on random songs that use
// every command, table row kind, transpose, repeat and tempo.
#include <cstring>
#include <memory>
#include <vector>

#include "../tools/oracle/cpu6502.h"
#include "compatibility/sid/sid_export.h"
#include "test.h"
#include "tracker/playback/player.h"

using namespace sidt;

namespace {

struct Sink : ISidSink {
    uint8_t regs[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { regs[chip & 1][reg & 31] = val; }
};

struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    int in(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
    bool chance(int percent) { return in(0, 99) < percent; }
};

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    Box() {
        song->patterns = pats.get();
        song->clear();
    }
};

void randomSong(Song& s, uint32_t seed, bool dual) {
    Rng r{seed * 2654435761u + 1};
    s.speed = uint8_t(r.in(1, 8));
    s.bpm = r.chance(70) ? 125 : (r.chance(50) ? 150 : uint8_t(r.in(60, 200)));
    s.sid.clock = r.chance(80) ? SidClock::kPal : SidClock::kNtsc;
    s.hrAD = uint8_t(r.next());
    s.hrSR = uint8_t(r.next());
    for (int t = 0; t < kTableKinds; ++t) {
        const int rows = r.in(8, 48);
        for (int i = 0; i < rows; ++i) {
            uint8_t L = uint8_t(r.next()), R = uint8_t(r.next());
            if (r.chance(8)) {
                L = 0xFF;
                R = r.chance(20) ? 0 : uint8_t(r.in(1, rows));
            }
            if (t == kSpeedTable && r.chance(70)) L = uint8_t(r.in(0, 6));
            s.tables[t].left[i] = L;
            s.tables[t].right[i] = R;
        }
    }
    const uint8_t waves[] = {0x09, 0x41, 0x21, 0x11, 0x81, 0x00, 0xFE, 0xFF, 0x15};
    const uint8_t gates[] = {0, 1, 2, 2, 3, 0x42, 0x82, 0xC2, 5};
    for (int i = 1; i <= 8; ++i) {
        Instrument& in = s.inst[i];
        in.ad = uint8_t(r.next());
        in.sr = uint8_t(r.next());
        in.firstWave = waves[r.in(0, 8)];
        in.wavePtr = uint8_t(r.chance(80) ? r.in(1, 40) : 0);
        in.pulsePtr = uint8_t(r.chance(60) ? r.in(1, 40) : 0);
        in.filterPtr = uint8_t(r.chance(40) ? r.in(1, 40) : 0);
        in.vibParam = uint8_t(r.chance(50) ? r.in(1, 40) : 0);
        in.vibDelay = uint8_t(r.chance(50) ? r.in(0, 6) : 0);
        in.gateTimer = gates[r.in(0, 8)];
    }
    const int npat = r.in(1, 4);
    for (int p = 0; p < npat; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(uint8_t(r.in(1, 40)));
        pt.starts = uint8_t(r.next() & 0x3F);
        for (int row = 0; row < pt.rows; ++row)
            for (int ch = 0; ch < (dual ? kChannels : 3); ++ch) {
                if (!r.chance(45)) continue;
                Cell& c = pt.cells[row][ch];
                const int k = r.in(0, 9);
                c.note = k < 7 ? uint8_t(r.in(1, 96)) : (k == 7 ? note::kKeyOff : (k == 8 ? note::kKeyOn : 0));
                c.inst = uint8_t(r.chance(50) ? r.in(1, 8) : 0);
                if (r.chance(50)) {
                    c.cmd = uint8_t(r.in(1, 15));
                    c.param = c.cmd <= 4 || c.cmd == 0xE ? uint8_t(r.in(0, 40)) : uint8_t(r.next());
                    if (c.cmd == 0xF) c.param = uint8_t(r.chance(70) ? r.in(0, 8) : r.next());
                }
            }
    }
    s.orderLen = uint8_t(r.in(1, 6));
    for (int i = 0; i < s.orderLen; ++i) {
        s.order[i].pattern = uint8_t(r.in(0, npat - 1));
        s.order[i].transpose = int8_t(r.chance(60) ? 0 : r.in(-24, 24));
        s.order[i].repeat = uint8_t(r.in(1, 3));
    }
    s.restartPos = uint8_t(r.in(0, s.orderLen));   // may be out of range: the player uses 0
}

// Frames that differ between the exported file and the player.
int compare(const Song& song, int frames, bool expectDual) {
    std::vector<uint8_t> file(psid::kWorkBytes);
    goat::Report rep;
    const uint32_t size = psid::exportSid(song, file.data(), uint32_t(file.size()), rep);
    CHECK(size > 0x7E);
    if (size <= 0x7E) return -1;
    CHECK_EQ(file[0x7A], expectDual ? 0x42 : 0);
    oracle::Cpu cpu;
    const uint16_t load = uint16_t(file[0x7C] | (file[0x7D] << 8));
    for (uint32_t i = 0x7E; i < size; ++i) cpu.mem[uint16_t(load + (i - 0x7E))] = file[i];
    const uint16_t init = uint16_t((file[10] << 8) | file[11]), play = uint16_t((file[12] << 8) | file[13]);
    if (!cpu.call(init, 0)) return -1;
    Sink sink;
    Player player;
    player.init(&song, &sink);
    player.playSong(0);
    int bad = 0;
    for (int f = 0; f < frames; ++f) {
        if (!cpu.call(play, 0)) return -1;
        player.tick();
        for (int c = 0; c < (expectDual ? 2 : 1); ++c)
            if (std::memcmp(cpu.sid + c * 32, sink.regs[c], 0x19)) {
                ++bad;
                break;
            }
    }
    return bad;
}

}  // namespace

TEST(random_songs_play_the_same_on_the_c64) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const bool dual = seed % 3 != 0;
        Box b;
        randomSong(*b.song, seed, dual);
        const int bad = compare(*b.song, 1200, dual);
        if (bad) std::printf("  seed %u: %d frames differ\n", unsigned(seed), bad);
        CHECK_EQ(bad, 0);
    }
}

TEST(header_and_check) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "TUNE");
    std::snprintf(s.author, sizeof s.author, "ME");
    s.pattern(0).cells[0][4] = {37, 1, 0, 0};
    s.bpm = 100;
    s.sid.model[1] = sid::Model::k6581;
    s.pattern(0).drum[0] = 1;
    std::vector<uint8_t> work(psid::kWorkBytes);
    const psid::ExportCheck c = psid::checkExport(s, work.data(), uint32_t(work.size()));
    CHECK(c.ok);
    CHECK(c.dual);
    CHECK(c.cia);
    CHECK_STR(c.report.line[0], "! PCM drum column is not exported");
    goat::Report rep;
    const uint32_t n = psid::exportSid(s, work.data(), uint32_t(work.size()), rep);
    CHECK_EQ(n, c.bytes);
    const uint8_t* h = work.data();
    CHECK(!std::memcmp(h, "PSID", 4));
    CHECK_EQ(h[5], 3);                 // v3: second SID
    CHECK_EQ(h[0x15], 1);              // CIA
    CHECK_STR(reinterpret_cast<const char*>(h + 0x16), "TUNE");
    CHECK_STR(reinterpret_cast<const char*>(h + 0x36), "ME");
    CHECK_EQ(h[0x77], 0x04 | 0x20 | 0x40);   // PAL, SID1 8580, SID2 6581
    CHECK_EQ(compare(s, 300, true), 0);
}

TEST(too_slow_for_a_cia_timer_is_refused) {
    // Fable review 2026-10-16: 32 bpm overflowed the 16-bit timer and played 6.7x too fast.
    Box b;
    b.song->bpm = 32;
    b.song->pattern(0).cells[0][0] = {37, 1, 0, 0};
    std::vector<uint8_t> work(psid::kWorkBytes);
    goat::Report rep;
    CHECK_EQ(psid::exportSid(*b.song, work.data(), uint32_t(work.size()), rep), 0u);
    CHECK(rep.lines > 0 && std::strstr(rep.line[0], "too slow"));
    b.song->bpm = 40;   // the slowest tempo that fits
    goat::Report ok;
    CHECK(psid::exportSid(*b.song, work.data(), uint32_t(work.size()), ok) > 0);
    CHECK_EQ(compare(*b.song, 100, false), 0);
}

TEST(multispeed_plays_on_a_cia_timer) {
    Box b;
    b.song->multi = 4;
    b.song->speed = 24;
    b.song->pattern(0).cells[0][0] = {37, 1, 0, 0};
    b.song->pattern(0).cells[8][4] = {49, 1, 0, 0};
    std::vector<uint8_t> work(psid::kWorkBytes);
    const psid::ExportCheck c = psid::checkExport(*b.song, work.data(), uint32_t(work.size()));
    CHECK(c.ok);
    CHECK(c.cia);
    bool noted = false;
    for (int i = 0; i < c.report.lines; ++i) noted = noted || std::strstr(c.report.line[i], "multispeed x4");
    CHECK(noted);
    goat::Report rep;
    CHECK(psid::exportSid(*b.song, work.data(), uint32_t(work.size()), rep) > 0);
    CHECK_EQ(work[0x15], 1);   // speed flag: CIA
    CHECK_EQ(compare(*b.song, 800, true), 0);
}
