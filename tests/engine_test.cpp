// Engine host tests: deterministic renders, tuning through the whole chain,
// dual-SID independence (APPROACH.md M1-M3 host checks; PLAN.md §18 DSP
// regressions compared numerically).
#include <cmath>
#include <vector>
#include <memory>
#include <vector>

#include "app/engine.h"
#include "test.h"

using namespace sidt;

namespace {

constexpr uint8_t C4 = 1 + 4 * 12;
constexpr uint8_t A4 = 1 + 4 * 12 + 9;

struct Rig {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    Song song;
    std::unique_ptr<Engine> eng{new Engine};
    Engine& engine = *eng;
    std::vector<int16_t> fxMem = std::vector<int16_t>(2 * 88200);
    std::vector<int16_t> kitMem = std::vector<int16_t>(512 * 1024);
    explicit Rig(uint32_t fs = 44100000) {
        song.patterns = pats.get();
        song.clear();
        engine.init(&song, fs);
        engine.setFxMemory(fxMem.data(), uint32_t(fxMem.size() / 2));
        engine.setKitMemory(kitMem.data(), uint32_t(kitMem.size()));
    }
    std::vector<int16_t> render(int blocks) {
        std::vector<int16_t> out(size_t(blocks) * 256);
        for (int b = 0; b < blocks; ++b) engine.fill(out.data() + b * 256, 128);
        return out;
    }
};

uint64_t fnv(const std::vector<int16_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (int16_t s : v) {
        h ^= uint16_t(s);
        h *= 1099511628211ull;
    }
    return h;
}

double hzOf(const std::vector<int16_t>& x, int ch, size_t skip, double fs) {
    double first = -1, last = -1;
    int n = 0;
    for (size_t i = skip + 1; i < x.size() / 2; ++i) {
        const double a = x[2 * (i - 1) + ch], b = x[2 * i + ch];
        if (a < 0 && b >= 0) {
            const double t = double(i - 1) + (-a) / (b - a);
            if (first < 0) first = t;
            else ++n;
            last = t;
        }
    }
    return n ? fs * n / (last - first) : 0;
}

// Saw at the note's pitch: wave table rows 0x30 "21 00", 0x31 "FF 00".
void sawInstrument(Song& s, int i) {
    Instrument& in = s.inst[i];
    in.ad = 0x00;
    in.sr = 0xF0;
    in.firstWave = 0x09;
    in.wavePtr = 0x30;
    in.pulsePtr = 0;
    s.tables[kWaveTable].left[0x2F] = 0x21;
    s.tables[kWaveTable].right[0x2F] = 0x00;
    s.tables[kWaveTable].left[0x30] = 0xFF;
    s.tables[kWaveTable].right[0x30] = 0x00;
}

}  // namespace

TEST(a4_through_player_and_engine_within_one_cent) {
    for (uint32_t fs : {44100000u, 44117647u}) {   // host and device rates
        Rig r(fs);
        sawInstrument(r.song, 2);
        r.song.pattern(0).cells[0][0] = {A4, 2, 0, 0};
        r.engine.playSong(0);
        const auto x = r.render(400);
        const double hz = hzOf(x, 0, 4096, fs / 1000.0);
        CHECK_NEAR(1200.0 * std::log2(hz / 440.0), 0.0, 1.0);
    }
}

TEST(render_is_deterministic) {
    auto run = [] {
        Rig r;
        r.song.pattern(0).cells[0][0] = {C4, 1, 0, 0};
        r.song.pattern(0).cells[4][3] = {C4 + 7, 1, 0, 0};
        r.engine.playSong(0);
        return fnv(r.render(200));
    };
    const uint64_t a = run(), b = run();
    CHECK_EQ(a, b);
}

TEST(six_voices_and_independent_models) {
    Rig r;
    for (int ch = 0; ch < kChannels; ++ch) r.song.pattern(0).cells[0][ch] = {uint8_t(C4 + 3 * ch), 1, 0, 0};
    // Filter both chips the same way through instrument 3's filter table.
    Instrument& in = r.song.inst[1];
    in.filterPtr = 1;
    Table& ft = r.song.tables[kFilterTable];
    ft.left[0] = 0x90; ft.right[0] = 0xA7;
    ft.left[1] = 0x00; ft.right[1] = 0x30;
    ft.left[2] = 0xFF; ft.right[2] = 0x00;
    r.song.sid.balance = 100;   // SID1 left, SID2 right
    r.song.sid.model[0] = sid::Model::k6581;
    r.song.sid.model[1] = sid::Model::k8580;
    r.engine.applySongConfig();
    r.engine.playSong(0);
    const auto x = r.render(300);
    double l = 0, rr = 0, diff = 0;
    for (size_t i = 128 * 50; i < x.size() / 2; ++i) {
        l += double(x[2 * i]) * x[2 * i];
        rr += double(x[2 * i + 1]) * x[2 * i + 1];
        diff += std::fabs(double(x[2 * i]) - x[2 * i + 1]);
    }
    CHECK(l > 0 && rr > 0);
    CHECK(diff > 1000.0);
    for (int ch = 0; ch < kChannels; ++ch) CHECK(r.engine.chip(ch / 3).envLevel(ch % 3) > 0);
    CHECK(r.engine.chip(0).model() == sid::Model::k6581);
    CHECK(r.engine.chip(1).model() == sid::Model::k8580);
}

TEST(mute_and_solo) {
    Rig r;
    sawInstrument(r.song, 2);
    r.song.pattern(0).cells[0][0] = {C4, 2, 0, 0};
    r.engine.playSong(0);
    r.render(20);
    r.engine.setMuted(0, true);
    auto x = r.render(50);
    int32_t peak = 0;
    for (size_t i = 128 * 30; i < x.size(); ++i) peak = std::max<int32_t>(peak, std::abs(x[i]));
    CHECK(peak < 50);   // DC blocker tail only
    r.engine.setMuted(0, false);
    r.engine.setSolo(3, true);   // solo another channel: channel 1 silent
    x = r.render(50);
    peak = 0;
    for (size_t i = 128 * 30; i < x.size(); ++i) peak = std::max<int32_t>(peak, std::abs(x[i]));
    CHECK(peak < 50);
}

TEST(stress_mode_and_profile) {
    Rig r;
    uint32_t counter = 0;
    static uint32_t* gCounter = nullptr;
    gCounter = &counter;
    r.engine.setCycleCounter([] { return ++*gCounter; });
    r.engine.setStress(true);
    const auto x = r.render(20);
    int32_t peak = 0;
    for (int16_t s : x) peak = std::max<int32_t>(peak, std::abs(s));
    CHECK(peak > 1000);
    const Profile p = r.engine.takeProfile();
    CHECK_EQ(p.blocks, 20u);
    CHECK(p.peak[Profile::kTotal] > 0);
    r.engine.setStress(false);
    r.render(2);
}

TEST(hold_outputs_silence) {
    Rig r;
    r.song.pattern(0).cells[0][0] = {C4, 1, 0, 0};
    r.engine.playSong(0);
    r.render(10);
    r.engine.hold();
    const auto x = r.render(2);
    for (int16_t s : x) CHECK_EQ(s, 0);
    r.engine.release();
}

TEST(stream_tap_sees_sample_times) {
    struct Tap : IStreamTap {
        std::vector<uint64_t> t;
        void onWrite(uint64_t sample, uint8_t, uint8_t, uint8_t) override { t.push_back(sample); }
    } tap;
    Rig r;
    r.engine.setTap(&tap);
    r.song.pattern(0).cells[0][0] = {C4, 1, 0, 0};
    r.song.pattern(0).cells[1][0] = {C4 + 1, 1, 0, 0};
    r.engine.playSong(0);
    r.render(60);
    CHECK(!tap.t.empty());
    // Writes happen only on tick boundaries (882 samples apart at 125 BPM).
    for (uint64_t s : tap.t) CHECK_EQ(s % 882 <= 1, true);
}

namespace {
double energy(const std::vector<int16_t>& x, size_t from, size_t to, int ch) {
    double e = 0;
    for (size_t i = from; i < to && 2 * i + ch < x.size(); ++i) e += double(x[2 * i + ch]) * x[2 * i + ch];
    return e;
}
// One short note on track 1 at row 0, then silence.
void blip(Song& s) {
    sawInstrument(s, 2);   // sustain F, release 0: about 6 ms after the key-off
    s.pattern(0).cells[0][0] = {C4, 2, 0, 0};
    s.pattern(0).cells[1][0] = {note::kKeyOff, 0, 0, 0};
}
}  // namespace

TEST(effects_off_is_bit_identical_and_global_switch) {
    auto run = [](bool fx, bool global) {
        Rig r;
        blip(r.song);
        if (fx) {
            r.song.fx.delayMix = 50;
            r.song.fx.reverbMix = 30;
        }
        r.engine.setEffects(global);
        r.engine.playSong(0);
        return r.render(200);
    };
    const auto dry = run(false, true);
    CHECK(run(false, false) == dry);
    CHECK(run(true, false) == dry);   // global switch off: no effects
    CHECK(!(run(true, true) == dry));
}

TEST(delay_echo_after_its_time) {
    Rig r;
    blip(r.song);
    r.song.fx.delayMix = 80;
    r.song.fx.delayTicks = 12;   // 12 x 882 samples
    r.song.fx.delayFeedback = 0;
    r.engine.playSong(0);
    const auto x = r.render(300);
    const size_t d = 12 * 882;
    // The blip lasts about 2 ticks (1764 samples); its release decays fast.
    const double early = energy(x, d - 3000, d - 200, 0);
    const double echo = energy(x, d, d + 1600, 0);
    CHECK(echo > early * 20.0);
}

TEST(drums_trigger_from_the_drum_column) {
    Rig r;
    r.song.pattern(0).drum[0] = 1;   // kick
    r.song.pattern(0).drum[4] = 4;   // closed hat
    uint32_t counter = 0;
    static uint32_t* gc = nullptr;
    gc = &counter;
    r.engine.setCycleCounter([] { return ++*gc; });
    r.engine.playSong(0);
    const auto x = r.render(60);
    CHECK(energy(x, 0, 2000, 0) > 1e9);
    const Profile p = r.engine.takeProfile();
    CHECK(p.peak[Profile::kPcm] > 0);
    // Muting the drum column (track 7) silences them.
    Rig m;
    m.song.pattern(0).drum[0] = 1;
    m.engine.setMuted(kChannels, true);
    m.engine.playSong(0);
    CHECK(energy(m.render(30), 0, 3000, 0) < 1e6);
}

TEST(voice_pan_hard_left) {
    Rig r;
    sawInstrument(r.song, 2);
    r.song.sid.balance = 0;
    r.song.pattern(0).cells[0][1] = {C4, 2, 0, 0};
    r.song.mod.pan[1] = -100;
    r.engine.applySongConfig();
    r.engine.playSong(0);
    const auto x = r.render(100);
    const double l = energy(x, 4000, 12000, 0), rr = energy(x, 4000, 12000, 1);
    CHECK(l > 1e9);
    CHECK(rr < l * 1e-4);
}
