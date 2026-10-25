// GoatTracker format and import host tests (PLAN.md §18: GoatTracker parsing,
// import/export round trips). Synthetic files are built here byte by byte
// from the documented format; songs produced by GoatTracker itself are used
// when present in fixtures/private/goattracker (git-ignored, not
// redistributable; tests skip without them).
#include <array>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "compatibility/goat/export.h"
#include "compatibility/goat/import.h"
#include "tracker/playback/player.h"
#include "test.h"
#include "tracker/commands/commands.h"
#include "tracker/song/sound_library.h"
#include "../tools/demo/classic_instruments.h"

using namespace sidt;
using Bytes = std::vector<uint8_t>;

namespace {

struct VecSink : goat::Sink {
    Bytes b;
    bool put(const void* d, uint32_t n) override {
        b.insert(b.end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
        return true;
    }
};

void name32(Bytes& b, const char* s) {
    for (int i = 0; i < 32; ++i) b.push_back(i < int(std::strlen(s)) ? uint8_t(s[i]) : 0);
}

// Two-channel song: ch1 plays pattern 0 (4 rows) twice, transposed +12 the
// second time, then loops to the second entry; ch2 plays pattern 1 (8
// rows); ch3 an empty 8-row pattern 2.
Bytes demoSong() {
    Bytes b = {'G', 'T', 'S', '5'};
    name32(b, "DEMO");
    name32(b, "ME");
    name32(b, "");
    b.push_back(1);   // subtunes
    const Bytes orders[3] = {{0x00, 0xFC, 0x00, 0xFF}, {0x01, 0xFF}, {0x02, 0xFF}};
    const uint8_t restart[3] = {1, 0, 0};
    for (int c = 0; c < 3; ++c) {
        b.push_back(uint8_t(orders[c].size()));
        b.insert(b.end(), orders[c].begin(), orders[c].end());
        b.push_back(restart[c]);
    }
    b.push_back(1);   // instruments
    const uint8_t inst[9] = {0x09, 0xA9, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x09};
    b.insert(b.end(), inst, inst + 9);
    const char* nm = "LEAD";
    for (int i = 0; i < 16; ++i) b.push_back(i < 4 ? uint8_t(nm[i]) : 0);
    // Tables: wave 41 00 / FF 00, others empty.
    b.insert(b.end(), {2, 0x41, 0xFF, 0x00, 0x00, 0, 0, 0});
    b.push_back(3);   // patterns
    auto pattern = [&](std::vector<std::array<uint8_t, 4>> rows) {
        b.push_back(uint8_t(rows.size() + 1));
        for (auto& r : rows) b.insert(b.end(), r.begin(), r.end());
        b.insert(b.end(), {0xFF, 0, 0, 0});
    };
    pattern({{0x60 + 48, 1, 0, 0}, {0xBD, 0, 0, 0}, {0xBE, 0, 0, 0}, {0xBD, 0, 0xA, 0x42}});
    pattern({{0x60 + 36, 1, 4, 1}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0},
             {0x60 + 38, 1, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBF, 0, 0, 0}});
    pattern({{0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0},
             {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}, {0xBD, 0, 0, 0}});
    return b;
}

// Register state of SID1 after each of `ticks` player ticks.
struct Trace : ISidSink {
    uint8_t regs[2][32] = {};
    std::vector<uint8_t> all;
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { regs[chip & 1][reg & 31] = val; }
};
std::vector<uint8_t> play(const Song& s, int ticks) {
    Trace t;
    Player p;
    p.init(&s, &t);
    p.playSong(0);
    for (int i = 0; i < ticks; ++i) {
        p.tick();
        t.all.insert(t.all.end(), t.regs[0], t.regs[0] + 0x19);
    }
    return t.all;
}

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> s{new Song};
    Box() {
        s->patterns = pats.get();
        s->clear();
    }
};

Bytes readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string privateDir() {
    const char* d = std::getenv("SIDT_ROOT");
    return std::string(d ? d : "..") + "/fixtures/private/goattracker";
}

}  // namespace

TEST(read_and_write_back_byte_equal) {
    const Bytes in = demoSong();
    std::unique_ptr<goat::Song> gt(new goat::Song);
    CHECK(goat::readSong({in.data(), uint32_t(in.size())}, *gt) == goat::Result::kOk);
    CHECK_STR(gt->name, "DEMO");
    CHECK_EQ(gt->patterns, 3);
    CHECK_EQ(gt->rows[0], 4);
    CHECK_EQ(gt->rows[1], 8);
    CHECK_EQ(gt->orderLen[0][0], 4);
    CHECK_EQ(gt->orderRestart[0][0], 1);
    CHECK_EQ(gt->inst[1].ad, 0x09);
    CHECK_STR(gt->inst[1].name, "LEAD");
    VecSink out;
    CHECK(goat::writeSong(*gt, out));
    CHECK(out.b == in);
}

TEST(rejects_bad_files) {
    std::unique_ptr<goat::Song> gt(new goat::Song);
    const Bytes old = {'G', 'T', 'S', '4', 0};
    CHECK(goat::readSong({old.data(), uint32_t(old.size())}, *gt) == goat::Result::kOldFormat);
    const Bytes junk = {'M', 'T', 'h', 'd'};
    CHECK(goat::readSong({junk.data(), uint32_t(junk.size())}, *gt) == goat::Result::kBadMagic);
    Bytes cut = demoSong();
    cut.resize(cut.size() - 10);
    CHECK(goat::readSong({cut.data(), uint32_t(cut.size())}, *gt) != goat::Result::kOk);
}

TEST(import_flattens_channels_with_transpose_and_loop) {
    const Bytes in = demoSong();
    std::unique_ptr<goat::Song> gt(new goat::Song);
    CHECK(goat::readSong({in.data(), uint32_t(in.size())}, *gt) == goat::Result::kOk);
    Box b;
    goat::Report rep;
    CHECK(goat::importSong(*gt, 0, *b.s, rep));
    Song& s = *b.s;
    CHECK_STR(s.name, "DEMO");
    CHECK_EQ(s.inst[1].wavePtr, 1);
    CHECK_EQ(s.inst[1].gateTimer, 2);
    CHECK_EQ(s.tables[kWaveTable].left[0], 0x41);
    // ch1 intro 4 rows (first pass of pattern 0), loop = 4 rows (+12);
    // ch2 loop 8 rows: intro 4, loop lcm(4, 8) = 8 -> 12 rows.
    uint32_t rows = 0;
    for (int i = 0; i < s.orderLen; ++i) rows += s.pattern(s.order[i].pattern).rows * s.order[i].repeat;
    CHECK_EQ(rows, 12u);
    // Row 0: C-4 on track 1, C-3 with 4XY on track 2.
    const Pattern& p0 = s.pattern(s.order[0].pattern);
    CHECK_EQ(p0.cells[0][0].note, 1 + 48);
    CHECK_EQ(p0.cells[0][1].note, 1 + 36);
    CHECK_EQ(p0.cells[0][1].cmd, 4);
    CHECK_EQ(p0.cells[2][0].note, note::kKeyOff);
    CHECK_EQ(p0.cells[3][0].cmd, 0xA);
    CHECK_EQ(p0.cells[3][0].param, 0x42);
    // The loop starts at row 4 where track 1 plays pattern 0 transposed +12.
    uint32_t at = 0;
    int restartRow = -1;
    for (int i = 0; i < s.orderLen; ++i) {
        if (i == s.restartPos) restartRow = int(at);
        at += s.pattern(s.order[i].pattern).rows * s.order[i].repeat;
    }
    CHECK_EQ(restartRow, 4);
    const Pattern& p1 = s.pattern(s.order[s.restartPos].pattern);
    CHECK_EQ(p1.cells[0][0].note, 1 + 60);
    CHECK_EQ(p1.cells[3][1].note, note::kKeyOn);   // ch2 row 7
    CHECK_EQ(p1.starts & 1, 1);                    // track 1 starts a GT pattern here
    CHECK_EQ(rep.directPercent(), 100u);
}

TEST(instrument_import_relocates_tables) {
    goat::InsFile ins;
    ins.inst.ad = 0x12;
    ins.inst.wavePtr = 1;
    ins.inst.pulsePtr = 1;
    std::strcpy(ins.inst.name, "BELL");
    ins.tables.len[0] = 2;
    ins.tables.left[0][0] = 0x11; ins.tables.right[0][0] = 0x00;
    ins.tables.left[0][1] = 0xFF; ins.tables.right[0][1] = 0x01;   // loop to row 1
    ins.tables.len[1] = 1;
    ins.tables.left[1][0] = 0x88; ins.tables.right[1][0] = 0x00;
    VecSink out;
    CHECK(goat::writeIns(ins, out));
    goat::InsFile back;
    CHECK(goat::readIns({out.b.data(), uint32_t(out.b.size())}, back) == goat::Result::kOk);
    Box b;   // default song: wave rows 1-2 and pulse rows 1-4 used
    goat::Report rep;
    CHECK(goat::importInstrument(back, 5, *b.s, rep));
    const Instrument& in = b.s->inst[5];
    CHECK_STR(in.name, "BELL");
    CHECK_EQ(in.wavePtr, 3);
    CHECK_EQ(in.pulsePtr, 5);
    CHECK_EQ(b.s->tables[kWaveTable].left[2], 0x11);
    CHECK_EQ(b.s->tables[kWaveTable].right[3], 3);   // jump relocated
}

TEST(goattracker_produced_songs_when_present) {
    const std::string dir = privateDir();
    DIR* d = opendir(dir.c_str());
    if (!d) {
        std::printf("  (skipped: no %s)\n", dir.c_str());
        return;
    }
    int songs = 0;
    for (dirent* e; (e = readdir(d));) {
        const std::string n = e->d_name;
        if (n.size() < 5 || n.substr(n.size() - 4) != ".sng") continue;
        const Bytes in = readFile(dir + "/" + n);
        std::unique_ptr<goat::Song> gt(new goat::Song);
        const goat::Result r = goat::readSong({in.data(), uint32_t(in.size())}, *gt);
        CHECK(r == goat::Result::kOk);
        if (r != goat::Result::kOk) continue;
        VecSink out;
        CHECK(goat::writeSong(*gt, out));
        if (out.b != in) std::printf("  FAIL byte round trip: %s\n", n.c_str());
        CHECK(out.b == in);
        Box b;
        goat::Report rep;
        const bool ok = goat::importSong(*gt, 0, *b.s, rep);
        std::printf("  %-20s %s: %s\n", n.c_str(), ok ? "imported" : "NOT imported", rep.lines ? rep.line[0] : "");
        ++songs;
    }
    closedir(d);
    CHECK(songs > 0);
}

TEST(native_song_exports_and_reimports) {
    Box a;
    Song& s = *a.s;
    s.speed = 5;
    s.orderLen = 3;
    s.order[0] = {0, 0, 1};
    s.order[1] = {1, 3, 2};
    s.order[2] = {0, -2, 1};
    s.restartPos = 1;
    s.pattern(0).rows = 16;
    s.pattern(1).rows = 8;
    s.pattern(0).cells[0][0] = {1 + 48, 1, 0, 0};
    s.pattern(0).cells[4][1] = {1 + 52, 1, cmd::kVibrato, 1};
    s.pattern(0).cells[8][2] = {note::kKeyOff, 0, 0, 0};
    s.pattern(1).cells[0][0] = {1 + 55, 1, cmd::kSetAD, 0x22};
    s.tables[kSpeedTable].left[0] = 3;
    s.tables[kSpeedTable].right[0] = 0x10;
    goat::ExportCheck chk = goat::checkExport(s);
    CHECK(chk.ok);
    std::unique_ptr<goat::Song> gt(new goat::Song);
    goat::Report rep;
    CHECK(goat::exportSong(s, *gt, rep));
    // Pattern 0: 3 tracks, track 1 twice (the first play carries the F05 for
    // the initial speed); pattern 1: its two empty tracks are shared.
    CHECK_EQ(gt->patterns, 6);
    VecSink out;
    CHECK(goat::writeSong(*gt, out));
    std::unique_ptr<goat::Song> back(new goat::Song);
    CHECK(goat::readSong({out.b.data(), uint32_t(out.b.size())}, *back) == goat::Result::kOk);
    Box b;
    CHECK(goat::importSong(*back, 0, *b.s, rep));
    // Same music on tracks 1-3: identical register streams.
    b.s->speed = 6;   // the importer starts at GT's tempo 6; the F05 on row 0 sets 5
    CHECK(play(s, 600) == play(*b.s, 600));
}

TEST(export_check_lists_losses) {
    Box a;
    Song& s = *a.s;
    s.bpm = 140;
    s.pattern(0).cells[0][4] = {1 + 40, 1, 0, 0};   // SID2 content
    s.pattern(0).cells[1][0] = {96, 1, 0, 0};        // B-7
    goat::ExportCheck c = goat::checkExport(s);
    CHECK(!c.notes);
    bool sid2 = false, bpm = false;
    for (int i = 0; i < c.report.lines; ++i) {
        sid2 = sid2 || std::strstr(c.report.line[i], "SID2");
        bpm = bpm || std::strstr(c.report.line[i], "BPM");
    }
    CHECK(sid2);
    CHECK(bpm);
}

TEST(instrument_export_takes_reachable_rows) {
    Box a;
    Song& s = *a.s;
    // Wave rows 10-12 loop 11..12; pulse row 20; vibrato speed row 5.
    Instrument& in = s.inst[3];
    in.wavePtr = 10;
    in.pulsePtr = 20;
    in.vibParam = 5;
    in.vibDelay = 4;
    s.tables[kWaveTable].left[9] = 0x41;
    s.tables[kWaveTable].left[10] = 0x21;
    s.tables[kWaveTable].left[11] = 0xFF; s.tables[kWaveTable].right[11] = 11;
    s.tables[kPulseTable].left[19] = 0x88;
    s.tables[kSpeedTable].left[4] = 2; s.tables[kSpeedTable].right[4] = 0x30;
    goat::InsFile f;
    CHECK(goat::exportInstrument(s, 3, f));
    CHECK_EQ(f.inst.wavePtr, 1);
    CHECK_EQ(f.tables.len[kWaveTable], 3);
    CHECK_EQ(f.tables.right[kWaveTable][2], 2);   // jump to row 11 -> 2
    CHECK_EQ(f.inst.pulsePtr, 1);
    CHECK_EQ(f.inst.vibParam, 1);
    CHECK_EQ(f.tables.right[kSpeedTable][0], 0x30);
    // And back into another song at other rows.
    Box b;
    goat::Report rep;
    CHECK(goat::importInstrument(f, 7, *b.s, rep));
    const Instrument& got = b.s->inst[7];
    CHECK_EQ(b.s->tables[kWaveTable].l(got.wavePtr), 0x41);
    CHECK_EQ(b.s->tables[kWaveTable].r(got.wavePtr + 2), got.wavePtr + 1);
}

TEST(goattracker_songs_survive_export_when_present) {
    const std::string dir = privateDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    int n = 0;
    for (dirent* e; (e = readdir(d));) {
        const std::string name = e->d_name;
        if (name.size() < 5 || name.substr(name.size() - 4) != ".sng") continue;
        const Bytes in = readFile(dir + "/" + name);
        std::unique_ptr<goat::Song> gt(new goat::Song);
        if (goat::readSong({in.data(), uint32_t(in.size())}, *gt) != goat::Result::kOk) continue;
        Box a, b;
        goat::Report rep;
        if (!goat::importSong(*gt, 0, *a.s, rep)) continue;
        std::unique_ptr<goat::Song> ex(new goat::Song);
        const bool ok = goat::exportSong(*a.s, *ex, rep);
        CHECK(ok);
        if (!ok) {
            std::printf("  export failed: %s: %s\n", name.c_str(), rep.lines ? rep.line[0] : "");
            continue;
        }
        VecSink out;
        goat::writeSong(*ex, out);
        std::unique_ptr<goat::Song> back(new goat::Song);
        CHECK(goat::readSong({out.b.data(), uint32_t(out.b.size())}, *back) == goat::Result::kOk);
        CHECK(goat::importSong(*back, 0, *b.s, rep));
        const bool same = play(*a.s, 3000) == play(*b.s, 3000);
        if (!same) std::printf("  export changes playback: %s\n", name.c_str());
        CHECK(same);
        ++n;
    }
    closedir(d);
    std::printf("  %d GoatTracker songs: import -> export -> import plays the same\n", n);
}

TEST(export_keeps_init_slots_that_patterns_play) {
    // Fable review 2026-10-22: an untouched (init patch) slot played on a
    // track was left out of the .sng, so GoatTracker played it silent.
    Box b;
    Song& s = *b.s;
    s.pattern(0).cells[0][0] = {37, 3, 0, 0};
    std::unique_ptr<goat::Song> gt(new goat::Song);
    goat::Report rep;
    CHECK(goat::exportSong(s, *gt, rep));
    CHECK(gt->instruments >= 3);
    CHECK_EQ(gt->inst[3].sr, 0xA9);
}

TEST(import_keeps_slots_above_the_count_silent) {
    // Fable review 2026-10-22: GoatTracker plays them as zero instruments.
    Box a, b;
    a.s->pattern(0).cells[0][0] = {37, 1, 0, 0};
    std::unique_ptr<goat::Song> gt(new goat::Song);
    goat::Report rep;
    CHECK(goat::exportSong(*a.s, *gt, rep));
    CHECK_EQ(gt->instruments, 1);
    CHECK(goat::importSong(*gt, 0, *b.s, rep));
    CHECK_EQ(b.s->inst[2].sr, Instrument::kLegacySr);
    CHECK_EQ(b.s->inst[2].firstWave, Instrument::kLegacyWave);
}

TEST(every_preset_survives_the_ins_round_trip) {
    // UX plan P3.2: the stock library on the card is the presets as .ins
    // files; loaded from the card they must play exactly like the preset.
    int bad = 0;
    const int nClassic = int(sizeof classic::kClassic / sizeof classic::kClassic[0]);
    for (int i = 0; i < library::presetCount() + nClassic; ++i) {
        const library::Preset& pre =
            i < library::presetCount() ? library::preset(i) : classic::kClassic[i - library::presetCount()];
        Box a, b;
        for (Box* x : {&a, &b}) {
            for (Table& t : x->s->tables) t = Table{};
            x->s->pattern(0).cells[0][0] = {37, 1, 0, 0};
            x->s->pattern(0).cells[16][0] = {44, 1, 0, 0};
            x->s->pattern(0).cells[30][0] = {note::kKeyOff, 0, 0, 0};
        }
        CHECK(library::load(*a.s, 1, pre, 0) == library::Result::kOk);
        goat::InsFile ins;
        CHECK(goat::exportInstrument(*a.s, 1, ins));
        VecSink sink;
        CHECK(goat::writeIns(ins, sink));
        goat::InsFile back;
        CHECK(goat::readIns({sink.b.data(), uint32_t(sink.b.size())}, back) == goat::Result::kOk);
        goat::Report rep;
        CHECK(goat::importInstrument(back, 1, *b.s, rep));
        if (play(*a.s, 300) != play(*b.s, 300)) {
            std::printf("  %s plays differently after .ins\n", pre.name);
            ++bad;
        }
    }
    CHECK_EQ(bad, 0);
}
