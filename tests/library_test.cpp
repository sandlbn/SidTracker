// Sound library and arpeggiator insert: every preset loads, plays and keeps
// its jumps inside its own rows; reloading reuses rows; a full table changes
// nothing; arps play their notes in order.
#include <cstring>
#include <memory>

#include "test.h"
#include "tracker/playback/player.h"
#include "tracker/song/sound_library.h"

using namespace sidt;

namespace {

struct Regs : ISidSink {
    uint8_t r[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { r[chip & 1][reg & 31] = val; }
};

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

bool sameTables(const Song& a, const Song& b) {
    return !std::memcmp(a.tables, b.tables, sizeof a.tables);
}

}  // namespace

TEST(every_preset_loads_and_sounds) {
    CHECK(library::presetCount() >= 40);
    for (int i = 0; i < library::presetCount(); ++i) {
        const library::Preset& p = library::preset(i);
        Box b;
        Song& s = *b.song;
        CHECK(library::load(s, 1, p, 0) == library::Result::kOk);
        CHECK_STR(s.inst[1].name, p.name);
        // Jumps stay inside the instrument's programs.
        for (int t = 0; t < kTableKinds; ++t)
            for (int r = 1; r <= s.tables[t].used(); ++r)
                if (s.tables[t].l(r) == 0xFF) CHECK(s.tables[t].r(r) <= s.tables[t].used());
        // Play C-4 on track 1: the voice gets a waveform, the gate and a pitch.
        s.pattern(0).cells[0][0] = {49, 1, 0, 0};
        Regs regs;
        Player pl;
        pl.init(&s, &regs);
        pl.playSong(0);
        bool sounded = false;
        for (int t = 0; t < 4; ++t) {
            pl.tick();
            const uint8_t ctl = regs.r[0][4];
            if ((ctl & 0xF0) && (ctl & 1) && (regs.r[0][0] | regs.r[0][1])) sounded = true;
        }
        if (!sounded) std::printf("  silent preset: %s\n", p.name);
        CHECK(sounded);
    }
}

TEST(reload_reuses_rows_and_full_table_changes_nothing) {
    Box b;
    Song& s = *b.song;
    const library::Preset& p = library::preset(0);
    CHECK(library::load(s, 1, p, 0) == library::Result::kOk);
    const int used = s.tables[kWaveTable].used();
    CHECK(library::load(s, 2, p, 0) == library::Result::kOk);
    CHECK_EQ(s.tables[kWaveTable].used(), used);
    CHECK_EQ(s.inst[2].wavePtr, s.inst[1].wavePtr);
    // Fill the wave table to the last row: the next load must fail cleanly.
    for (int r = used; r < kTableLen; ++r) s.tables[kWaveTable].left[r] = 0x41;
    Box copy;
    std::memcpy(copy.song->tables, s.tables, sizeof s.tables);
    const Instrument before = s.inst[3];
    CHECK(library::load(s, 3, library::preset(library::categoryFirst(library::kDrum)), 0) ==
          library::Result::kTableFull);
    CHECK(sameTables(s, *copy.song));
    CHECK(!std::memcmp(&before, &s.inst[3], sizeof before));
}

TEST(filter_routes_the_cursor_voice) {
    Box b;
    Song& s = *b.song;
    int acid = -1;
    for (int i = 0; i < library::presetCount(); ++i)
        if (!std::strcmp(library::preset(i).name, "ACID BASS")) acid = i;
    CHECK(acid >= 0);
    CHECK(library::load(s, 1, library::preset(acid), 2) == library::Result::kOk);
    CHECK_EQ(s.tables[kFilterTable].r(s.inst[1].filterPtr) & 0x0F, 0x04);   // voice 3
}

TEST(arpeggiator_plays_its_chord) {
    Box b;
    Song& s = *b.song;
    s.inst[1].ad = 0x00;
    s.inst[1].sr = 0xF0;
    library::Arp a;
    a.chord = library::kMinor7;
    a.order = library::kUpDown;
    a.ticks = 2;
    a.wave = library::kPulse;
    int8_t notes[10];
    const int n = library::arpNotes(a, notes, 10);
    CHECK_EQ(n, 6);   // 0 3 7 10 7 3
    CHECK_EQ(notes[3], 10);
    CHECK_EQ(notes[5], 3);
    CHECK(library::insertArp(s, 1, a) == library::Result::kOk);
    CHECK(s.inst[1].pulsePtr != 0);   // a pulse arp got a pulse width
    s.pattern(0).cells[0][0] = {49, 1, 0, 0};
    Regs regs;
    Player pl;
    pl.init(&s, &regs);
    pl.playSong(0);
    pl.tick();   // note init (first wave)
    FreqTable ft;
    ft.build(sid::kPalClockHz);
    for (int step = 0; step < 2 * n * 2; ++step) {
        pl.tick();
        const uint16_t f = uint16_t(regs.r[0][0] | (regs.r[0][1] << 8));
        CHECK_EQ(f, ft.freq(48 + notes[(step / 2) % n]));
    }
}
