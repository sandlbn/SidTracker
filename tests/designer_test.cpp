// Sound designer: designs survive apply + decode, knob turns rewrite in
// place (the tables do not grow), shared rows are never overwritten, and the
// player sends the designed values to the SID.
#include <cstring>
#include <memory>

#include "test.h"
#include "tracker/playback/player.h"
#include "tracker/song/designer.h"

using namespace sidt;
using designer::Design;

namespace {

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    Box() {
        song->patterns = pats.get();
        song->clear();
        for (Table& t : song->tables) t = Table{};
        for (int i = 1; i < kInstrumentCount; ++i) song->inst[i].clear(i);
    }
};

struct Regs : ISidSink {
    uint8_t r[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { r[chip & 1][reg & 31] = val; }
};

bool same(const Design& a, const Design& b) {
    return a.wave == b.wave && a.transient == b.transient && a.attack == b.attack && a.decay == b.decay &&
           a.sustain == b.sustain && a.release == b.release && a.pw == b.pw && a.pwmSpeed == b.pwmSpeed &&
           a.pwmTicks == b.pwmTicks && a.filter == b.filter && a.cutoff == b.cutoff && a.resonance == b.resonance &&
           a.sweep == b.sweep && (a.sweep == designer::kSweepOff || (a.sweepSpeed == b.sweepSpeed &&
                                                                     a.sweepTicks == b.sweepTicks)) &&
           a.vibDepth == b.vibDepth && (!a.vibDepth || (a.vibSpeed == b.vibSpeed && a.vibDelay == b.vibDelay)) &&
           a.hardRestart == b.hardRestart;
}

int usedRows(const Song& s) {
    int n = 0;
    for (const Table& t : s.tables) n += t.used();
    return n;
}

}  // namespace

TEST(designs_round_trip) {
    uint32_t x = 12345;
    auto rnd = [&](int n) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return int(x % uint32_t(n));
    };
    const uint8_t* waves;
    const int nw = designer::waveChoices(&waves);
    for (int i = 0; i < 300; ++i) {
        Box b;
        Design d;
        d.wave = waves[rnd(nw)];
        d.transient = uint8_t(rnd(designer::kTransients));
        d.attack = uint8_t(rnd(16));
        d.decay = uint8_t(rnd(16));
        d.sustain = uint8_t(rnd(16));
        d.release = uint8_t(rnd(16));
        d.pw = uint16_t(rnd(0x1000));
        d.pwmSpeed = uint8_t(rnd(0x80));
        d.pwmTicks = uint8_t(1 + rnd(0x7F));
        d.filter = uint8_t(rnd(designer::kFilterTypes));
        d.cutoff = uint8_t(rnd(256));
        d.resonance = uint8_t(rnd(16));
        d.sweep = uint8_t(rnd(designer::kSweeps));
        d.sweepSpeed = uint8_t(1 + rnd(0x7F));
        d.sweepTicks = uint8_t(1 + rnd(0x7F));
        d.vibDepth = uint8_t(rnd(3) ? 0 : rnd(256));
        d.vibSpeed = uint8_t(1 + rnd(0x7F));
        d.vibDelay = uint8_t(1 + rnd(255));
        d.hardRestart = rnd(2);
        if (d.filter == designer::kFilterOff) {   // nothing of the filter is stored
            d.cutoff = Design{}.cutoff;
            d.resonance = Design{}.resonance;
            d.sweep = designer::kSweepOff;
        }
        CHECK(designer::apply(*b.song, 3, d, 1) == designer::Result::kOk);
        const Design back = designer::decode(*b.song, 3);
        if (!same(d, back)) std::printf("  design %d does not round-trip\n", i);
        CHECK(same(d, back));
    }
}

TEST(knob_turns_rewrite_in_place) {
    Box b;
    Song& s = *b.song;
    Design d;
    d.filter = designer::kLowPass;
    d.sweep = designer::kWobble;
    d.vibDepth = 0x20;
    CHECK(designer::apply(s, 1, d, 0) == designer::Result::kOk);
    const int rows = usedRows(s);
    for (int v = 0; v < 200; ++v) {
        d.cutoff = uint8_t(v);
        d.resonance = uint8_t(v & 15);
        d.pw = uint16_t(v * 20);
        d.vibDepth = uint8_t(1 + v);
        d.sweep = uint8_t(v % designer::kSweeps);
        d.filter = uint8_t(v % designer::kFilterTypes);   // includes OFF and back on
        CHECK(designer::apply(s, 1, d, 0) == designer::Result::kOk);
    }
    CHECK_EQ(usedRows(s), rows);
}

TEST(shared_rows_are_not_overwritten) {
    Box b;
    Song& s = *b.song;
    Design d;
    d.filter = designer::kBandPass;
    CHECK(designer::apply(s, 1, d, 0) == designer::Result::kOk);
    s.inst[2] = s.inst[1];   // instrument 2 shares 1's programs
    Box copy;
    std::memcpy(copy.song->tables, s.tables, sizeof s.tables);
    d.cutoff = 0x10;
    d.pw = 0x123;
    CHECK(designer::apply(s, 2, d, 0) == designer::Result::kOk);
    CHECK(s.inst[2].pulsePtr != s.inst[1].pulsePtr);
    CHECK(s.inst[2].filterPtr != s.inst[1].filterPtr);
    // Instrument 1's rows are untouched.
    for (int k = 0; k < kTableKinds; ++k)
        for (int r = 1; r <= copy.song->tables[k].used(); ++r)
            CHECK(s.tables[k].l(r) == copy.song->tables[k].l(r) && s.tables[k].r(r) == copy.song->tables[k].r(r));
}

TEST(player_plays_the_design) {
    Box b;
    Song& s = *b.song;
    Design d;
    d.wave = 0x40;
    d.pw = 0x345;
    d.filter = designer::kHighPass;
    d.cutoff = 0x9A;
    d.resonance = 0xC;
    CHECK(designer::apply(s, 1, d, 2) == designer::Result::kOk);   // voice 3
    s.pattern(0).cells[0][2] = {49, 1, 0, 0};
    Regs regs;
    Player pl;
    pl.init(&s, &regs);
    pl.playSong(0);
    for (int i = 0; i < 4; ++i) pl.tick();
    CHECK_EQ(regs.r[0][14 + 4] & 0xF1, 0x41);                       // voice 3 pulse, gate
    CHECK_EQ(regs.r[0][14 + 2] | ((regs.r[0][14 + 3] & 15) << 8), 0x345);
    CHECK_EQ(regs.r[0][0x16], 0x9A);                                // cutoff
    CHECK_EQ(regs.r[0][0x17], 0xC4);                                // resonance C, voice 3
    CHECK_EQ(regs.r[0][0x18] & 0x70, 0x40);                         // high pass
}

TEST(rows_only_pattern_commands_use_are_not_rewritten) {
    // Fable review 2026-10-16: a filter program reached only by A01, and a
    // wave program reached only by 801.
    Box b;
    Song& s = *b.song;
    Design d;
    d.filter = designer::kLowPass;
    CHECK(designer::apply(s, 1, d, 0) == designer::Result::kOk);   // rows 1.. of each table
    const uint8_t fp = s.inst[1].filterPtr, wp = s.inst[1].wavePtr;
    s.inst[1].filterPtr = 0;                                     // only the command uses it now
    s.inst[1].wavePtr = 0;
    s.pattern(0).cells[0][0] = {0, 0, 0xA, fp};
    s.pattern(0).cells[1][0] = {0, 0, 0x8, wp};
    Box copy;
    std::memcpy(copy.song->tables, s.tables, sizeof s.tables);
    Design e;
    e.wave = 0x20;
    e.filter = designer::kHighPass;
    e.cutoff = 0x22;
    CHECK(designer::apply(s, 2, e, 0) == designer::Result::kOk);
    CHECK(s.inst[2].filterPtr != fp);
    CHECK(s.inst[2].wavePtr != wp);
    for (int r = 0; r < 5; ++r) {
        CHECK_EQ(s.tables[kFilterTable].l(fp + r), copy.song->tables[kFilterTable].l(fp + r));
        CHECK_EQ(s.tables[kFilterTable].r(fp + r), copy.song->tables[kFilterTable].r(fp + r));
    }
    CHECK_EQ(s.tables[kWaveTable].l(wp + 1), copy.song->tables[kWaveTable].l(wp + 1));
}
