// Native project file host tests (PLAN.md §15, §18 project serialization).
#include <cstring>
#include <memory>
#include <vector>

#include "test.h"
#include "tracker/song/project_file.h"

using namespace sidt;

namespace {

struct MemW : project::Writer {
    std::vector<uint8_t> b;
    bool write(const void* d, uint32_t n) override {
        b.insert(b.end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
        return true;
    }
};
struct MemR : project::Reader {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    explicit MemR(const std::vector<uint8_t>& v) : b(v) {}
    int32_t read(void* d, uint32_t n) override {
        const size_t k = std::min<size_t>(n, b.size() - pos);
        std::memcpy(d, b.data() + pos, k);
        pos += k;
        return int32_t(k);
    }
};

struct SongBox {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    Song s;
    SongBox() {
        s.patterns = pats.get();
        s.clear();
    }
};

void fillSong(Song& s) {
    std::strcpy(s.name, "ROUND TRIP");
    std::strcpy(s.author, "TEST");
    s.bpm = 150;
    s.speed = 5;
    s.mode = SongMode::kExtended;
    s.sid.model[0] = sid::Model::k6581;
    s.sid.clock = SidClock::kNtsc;
    s.sid.balance = 70;
    s.orderLen = 3;
    s.order[0] = {0, 0, 1};
    s.order[1] = {5, -12, 2};
    s.order[2] = {0, 3, 1};
    s.restartPos = 1;
    s.pattern(5).rows = 17;
    s.pattern(5).cells[16][5] = {12, 3, 0xA, 0x42};
    s.pattern(0).cells[0][0] = {note::kKeyOff, 0, 0, 0};
    s.pattern(77).cells[3][2] = {40, 1, 0xF, 3};   // unreferenced but not empty
    s.inst[3].ad = 0x12;
    s.inst[3].vibParam = 9;
    std::strcpy(s.inst[3].name, "BASS");
    s.tables[kSpeedTable].left[4] = 0x11;
    s.tables[kWaveTable].right[254] = 0x80;
}

}  // namespace

TEST(round_trip_preserves_everything) {
    SongBox a, b;
    fillSong(a.s);
    MemW w;
    CHECK(project::save(a.s, w) == project::Result::kOk);
    CHECK(std::memcmp(w.b.data(), "SIDT", 4) == 0);
    MemR r(w.b);
    CHECK(project::load(b.s, r) == project::Result::kOk);
    CHECK_STR(b.s.name, "ROUND TRIP");
    CHECK_STR(b.s.author, "TEST");
    CHECK_EQ(b.s.bpm, 150);
    CHECK_EQ(b.s.speed, 5);
    CHECK(b.s.mode == SongMode::kExtended);
    CHECK(b.s.sid.model[0] == sid::Model::k6581);
    CHECK(b.s.sid.model[1] == sid::Model::k8580);
    CHECK(b.s.sid.clock == SidClock::kNtsc);
    CHECK_EQ(b.s.sid.balance, 70);
    CHECK_EQ(b.s.orderLen, 3);
    CHECK_EQ(b.s.order[1].pattern, 5);
    CHECK_EQ(b.s.order[1].transpose, -12);
    CHECK_EQ(b.s.order[1].repeat, 2);
    CHECK_EQ(b.s.restartPos, 1);
    CHECK_EQ(b.s.pattern(5).rows, 17);
    CHECK_EQ(b.s.pattern(5).cells[16][5].param, 0x42);
    CHECK_EQ(b.s.pattern(0).cells[0][0].note, note::kKeyOff);
    CHECK_EQ(b.s.pattern(77).cells[3][2].note, 40);
    CHECK_EQ(b.s.inst[3].ad, 0x12);
    CHECK_EQ(b.s.inst[3].vibParam, 9);
    CHECK_STR(b.s.inst[3].name, "BASS");
    CHECK_STR(b.s.inst[4].name, "INSTR 04");
    CHECK_EQ(b.s.tables[kSpeedTable].left[4], 0x11);
    CHECK_EQ(b.s.tables[kWaveTable].right[254], 0x80);
    // A second save of the loaded song is byte-identical.
    MemW w2;
    CHECK(project::save(b.s, w2) == project::Result::kOk);
    CHECK(w.b == w2.b);
}

TEST(default_song_round_trips) {
    SongBox a, b;
    MemW w;
    CHECK(project::save(a.s, w) == project::Result::kOk);
    MemR r(w.b);
    CHECK(project::load(b.s, r) == project::Result::kOk);
    CHECK_STR(b.s.inst[1].name, "PULSE LEAD");
    CHECK_EQ(b.s.inst[1].pulsePtr, 1);
    CHECK_EQ(b.s.tables[kPulseTable].left[3], 0xFF);
}

TEST(unknown_chunks_are_skipped) {
    SongBox a, b;
    fillSong(a.s);
    MemW w;
    project::save(a.s, w);
    // Insert a future chunk right after the header.
    std::vector<uint8_t> f(w.b.begin(), w.b.begin() + 8);
    const uint8_t extra[] = {'X', 'T', 'R', 'A', 3, 0, 0, 0, 1, 2, 3};
    f.insert(f.end(), extra, extra + sizeof extra);
    f.insert(f.end(), w.b.begin() + 8, w.b.end());
    MemR r(f);
    CHECK(project::load(b.s, r) == project::Result::kOk);
    CHECK_STR(b.s.name, "ROUND TRIP");
}

TEST(rejects_bad_files) {
    SongBox b;
    std::vector<uint8_t> junk = {'R', 'I', 'F', 'F', 1, 0, 0, 0};
    MemR r1(junk);
    CHECK(project::load(b.s, r1) == project::Result::kBadMagic);
    std::vector<uint8_t> newer = {'S', 'I', 'D', 'T', 99, 0, 0, 0};
    MemR r2(newer);
    CHECK(project::load(b.s, r2) == project::Result::kNewerVersion);
    SongBox a;
    MemW w;
    project::save(a.s, w);
    std::vector<uint8_t> cut(w.b.begin(), w.b.begin() + long(w.b.size() / 2));
    MemR r3(cut);
    CHECK(project::load(b.s, r3) == project::Result::kCorrupt);
}

TEST(automation_round_trips_including_a_full_pattern) {
    SongBox a, b;
    a.s.pattern(0).setAuto(3, kAutoPw + 2, -1234);
    a.s.pattern(0).setAuto(3, kAutoRes + 1, 7);
    Pattern& full = a.s.pattern(9);   // unreferenced, every row and lane set
    full.rows = 128;
    for (int r = 0; r < 128; ++r)
        for (int l = 0; l < kAutoLanes; ++l) full.setAuto(r, l, int16_t(r * 16 + l - 1000));
    MemW w;
    CHECK(project::save(a.s, w) == project::Result::kOk);
    MemR r(w.b);
    CHECK(project::load(b.s, r) == project::Result::kOk);
    CHECK_EQ(b.s.pattern(0).autoSet[3], uint16_t((1u << (kAutoPw + 2)) | (1u << (kAutoRes + 1))));
    CHECK_EQ(b.s.pattern(0).autoVal[3][kAutoPw + 2], -1234);
    CHECK_EQ(b.s.pattern(0).autoVal[3][kAutoRes + 1], 7);
    CHECK_EQ(b.s.pattern(9).rows, 128);
    CHECK_EQ(b.s.pattern(9).autoSet[127], 0xFFFF);
    CHECK_EQ(b.s.pattern(9).autoVal[127][15], int16_t(127 * 16 + 15 - 1000));
    CHECK(b.s.extendedFeatures() & kExtAutomation);
}

TEST(extended_settings_and_drums_round_trip) {
    SongBox a, b;
    a.s.fx.delayMix = 33;
    a.s.fx.delayTicks = 9;
    a.s.fx.reverbMix = 21;
    a.s.drumVolume = 55;
    a.s.mod.lfo[1] = Lfo{8, Lfo::kSine, 77, 40};
    a.s.mod.pan[4] = -60;
    a.s.pattern(0).drum[0] = 1;
    a.s.pattern(0).drum[63] = 16;
    MemW w;
    CHECK(project::save(a.s, w) == project::Result::kOk);
    MemR r(w.b);
    CHECK(project::load(b.s, r) == project::Result::kOk);
    CHECK_EQ(b.s.fx.delayMix, 33);
    CHECK_EQ(b.s.fx.delayTicks, 9);
    CHECK_EQ(b.s.fx.reverbMix, 21);
    CHECK_EQ(b.s.drumVolume, 55);
    CHECK_EQ(b.s.mod.lfo[1].target, 8);
    CHECK_EQ(b.s.mod.lfo[1].shape, Lfo::kSine);
    CHECK_EQ(b.s.mod.lfo[1].period, 77);
    CHECK_EQ(b.s.mod.lfo[1].depth, 40);
    CHECK_EQ(b.s.mod.pan[4], -60);
    CHECK_EQ(b.s.pattern(0).drum[0], 1);
    CHECK_EQ(b.s.pattern(0).drum[63], 16);
    const uint32_t ext = b.s.extendedFeatures();
    CHECK((ext & kExtEffects) && (ext & kExtDrums) && (ext & kExtModulation));
}
