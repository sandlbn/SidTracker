// Table housekeeping (UX plan P1.4): inserting / deleting rows keeps every
// reference (instrument pointers, jumps, pattern commands, wave-table
// command rows) on the same program, the song plays the same, and pack
// removes only rows nothing reaches.
#include <cstring>
#include <memory>

#include "test.h"
#include "tracker/playback/player.h"
#include "tracker/song/tables.h"

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

// A song using every kind of reference: instrument 1 (default PULSE LEAD:
// wave 1-2, pulse 1-4 with a jump), instrument 2 with an arpeggio that
// loops (jump) and a wave-table command row pointing at the pulse table,
// 9xx / 8xx / 4xx pattern commands and a vibrato speed row.
void build(Song& s) {
    Table& w = s.tables[kWaveTable];
    // rows 3-6: arpeggio 0 4 7 loop to 3
    const uint8_t arp[][2] = {{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0xFF, 0x03}};
    for (int i = 0; i < 4; ++i) w.left[2 + i] = arp[i][0], w.right[2 + i] = arp[i][1];
    // rows 7-8: a wave command row F9 -> pulse row 1, then stop
    w.left[6] = 0xF9, w.right[6] = 0x01;
    w.left[7] = 0xFF, w.right[7] = 0x00;
    s.inst[2].wavePtr = 3;
    s.inst[2].pulsePtr = 1;
    s.tables[kSpeedTable].left[0] = 0x04;
    s.tables[kSpeedTable].right[0] = 0x20;
    s.inst[2].vibParam = 1;
    s.inst[2].vibDelay = 4;
    Pattern& p = s.pattern(0);
    p.cells[0][0] = {37, 1, 0, 0};
    p.cells[0][1] = {49, 2, 0x4, 0x01};   // vibrato from speed row 1
    p.cells[8][0] = {40, 1, 0x8, 0x07};   // wave pointer -> row 7
    p.cells[16][1] = {44, 2, 0x9, 0x02};  // pulse pointer -> row 2
}

// SID registers over n ticks.
std::unique_ptr<uint8_t[]> play(Song& s, int n) {
    std::unique_ptr<uint8_t[]> out(new uint8_t[size_t(n) * 32]);
    Regs reg;
    Player pl;
    pl.init(&s, &reg);
    pl.playSong(0);
    for (int i = 0; i < n; ++i) {
        pl.tick();
        std::memcpy(&out[size_t(i) * 32], reg.r[0], 32);
    }
    return out;
}

}  // namespace

TEST(insert_and_delete_rows_keep_the_song_the_same) {
    // Inserting at a program boundary keeps the sound (inside a program it
    // adds a step, as intended); deleting the inserted row restores the song.
    auto boundary = [](int k, int row) {
        if (k == kWaveTable) return row == 1 || row == 3 || row == 7 || row >= 9;
        if (k == kPulseTable) return row == 1 || row >= 5;
        return true;   // filter: unused; speed: single rows
    };
    for (int k = 0; k < kTableKinds; ++k)
        for (int row = 1; row <= 9; ++row) {
            Box a, b;
            build(*a.song);
            build(*b.song);
            CHECK(tables::insertRow(*b.song, TableKind(k), row));
            const int n = 400;
            auto ra = play(*a.song, n), rb = play(*b.song, n);
            const bool same = !std::memcmp(ra.get(), rb.get(), size_t(n) * 32);
            if (boundary(k, row) && !same) std::printf("  insert table %d row %d changed the sound\n", k, row);
            if (boundary(k, row)) CHECK(same);
            CHECK(tables::deleteRow(*b.song, TableKind(k), row));   // the empty row again
            CHECK(!std::memcmp(a.song->tables, b.song->tables, sizeof a.song->tables));
            CHECK(!std::memcmp(a.song->inst, b.song->inst, sizeof a.song->inst));
        }
}

TEST(insert_refuses_a_full_table) {
    Box a;
    a.song->tables[kPulseTable].left[kTableLen - 1] = 0x80;
    CHECK(!tables::insertRow(*a.song, kPulseTable, 1));
}

TEST(users_are_listed) {
    Box a;
    build(*a.song);
    char buf[128];
    CHECK_EQ(tables::describeUsers(*a.song, kWaveTable, 3, buf, sizeof buf), 2);   // INS 02 + the jump
    CHECK(std::strstr(buf, "INS 02") && std::strstr(buf, "jump from 06"));
    CHECK_EQ(tables::describeUsers(*a.song, kPulseTable, 1, buf, sizeof buf), 3);  // INS 01, INS 02, wave cmd
    CHECK(std::strstr(buf, "wave cmd 07"));
    CHECK_EQ(tables::describeUsers(*a.song, kWaveTable, 7, buf, sizeof buf), 1);   // 8xx in PAT 00
    CHECK(std::strstr(buf, "PAT 00"));
    CHECK_EQ(tables::describeUsers(*a.song, kWaveTable, 20, buf, sizeof buf), 0);
    CHECK_EQ(tables::describeUsers(*a.song, kWaveTable, 4, buf, sizeof buf), 1);   // inside INS 02's arpeggio
    CHECK(std::strstr(buf, "program of INS 02 from 03"));
}

TEST(pack_removes_only_unreached_rows) {
    Box a, b;
    build(*a.song);
    build(*b.song);
    // Orphans: a row between programs and two after them.
    CHECK(tables::insertRow(*b.song, kWaveTable, 3));
    b.song->tables[kWaveTable].left[2] = 0x11;   // unreached row 3
    b.song->tables[kWaveTable].left[10] = 0x21;  // unreached rows after the end
    b.song->tables[kWaveTable].left[11] = 0xFF;
    CHECK_EQ(tables::pack(*b.song, kWaveTable), 4);   // rows 03, 0A (empty), 0B, 0C
    CHECK(!std::memcmp(&a.song->tables[kWaveTable], &b.song->tables[kWaveTable], sizeof(Table)));
    CHECK(!std::memcmp(a.song->inst, b.song->inst, sizeof a.song->inst));
    CHECK_EQ(tables::pack(*b.song, kWaveTable), 0);
    const int n = 300;
    auto ra = play(*a.song, n), rb = play(*b.song, n);
    CHECK(!std::memcmp(ra.get(), rb.get(), size_t(n) * 32));
}

TEST(pack_removes_looping_orphan_programs) {
    // Fable review 2026-10-22: an orphan pulse program that loops (FF -> its
    // own second row) kept its loop alive and left a headless fragment.
    Box a;
    Table& p = a.song->tables[kPulseTable];
    const int used = p.used();   // PULSE LEAD rows 1-4
    const uint8_t orphan[][2] = {{0x81, 0x00}, {0x20, 0x30}, {0x20, 0xD0}, {0xFF, uint8_t(used + 2)}};
    for (int i = 0; i < 4; ++i) p.left[used + i] = orphan[i][0], p.right[used + i] = orphan[i][1];
    CHECK_EQ(tables::pack(*a.song, kPulseTable), 4);
    CHECK_EQ(p.used(), used);
}

TEST(speed_table_ff_rows_are_speeds_not_jumps) {
    // Fable review 2026-10-22: FF xx in the speed table is a note-independent
    // speed (bit 7 of the left byte), not a jump.
    Box a;
    Table& sp = a.song->tables[kSpeedTable];
    sp.left[1] = 0xFF;
    sp.right[1] = 0x05;
    a.song->pattern(0).cells[0][0] = {37, 1, 0x4, 0x02};
    CHECK(tables::insertRow(*a.song, kSpeedTable, 1));
    CHECK_EQ(sp.left[2], 0xFF);
    CHECK_EQ(sp.right[2], 0x05);                       // unchanged
    CHECK_EQ(a.song->pattern(0).cells[0][0].param, 3);   // the 4xx command followed
    char buf[64];
    CHECK_EQ(tables::describeUsers(*a.song, kSpeedTable, 6, buf, sizeof buf), 0);
}
