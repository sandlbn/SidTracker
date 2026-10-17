// SID engine host tests (PLAN.md §18: oscillator phase, ADSR transitions;
// APPROACH.md M1/M2 acceptance checks).
#include <cmath>
#include <vector>

#include "sid/sid.h"
#include "test.h"

using namespace sidt::sid;

namespace {

constexpr uint32_t kFs = 44100000;   // host rate, milli-Hz
constexpr double kClocksPerSample = double(kPalClockHz) / 44100.0;

void setup() {
    if (!tablesReady()) initTables(kFs);
}

void voice(Chip& c, int v, uint16_t freq, uint16_t pw, uint8_t ctrl, uint8_t ad = 0x00, uint8_t sr = 0xF0) {
    const uint8_t b = uint8_t(v * kVoiceStride);
    c.write(uint8_t(b + kFreqLo), uint8_t(freq));
    c.write(uint8_t(b + kFreqHi), uint8_t(freq >> 8));
    c.write(uint8_t(b + kPwLo), uint8_t(pw));
    c.write(uint8_t(b + kPwHi), uint8_t(pw >> 8));
    c.write(uint8_t(b + kAD), ad);
    c.write(uint8_t(b + kSR), sr);
    c.write(uint8_t(b + kControl), ctrl);
}

std::vector<float> render(Chip& c, int n) {
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; i += kMaxRender) {
        const int k = n - i < kMaxRender ? n - i : kMaxRender;
        c.render(k, out.data() + i);
    }
    return out;
}

// Frequency from linearly interpolated rising zero crossings.
double measureHz(const std::vector<float>& x, int skip) {
    double first = -1, last = -1;
    int count = 0;
    for (size_t i = size_t(skip) + 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
            if (first < 0) first = t;
            else ++count;
            last = t;
        }
    }
    return count ? 44100.0 * count / (last - first) : 0.0;
}

double rms(const std::vector<float>& x, int skip) {
    double s = 0;
    for (size_t i = size_t(skip); i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(x.size() - size_t(skip)));
}

}  // namespace

TEST(accumulator_follows_sid_formula) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    voice(c, 0, 7493, 0x800, kSaw | kGate);
    const int n = 10000;
    render(c, n);
    const double exact = std::fmod(double(n) * 7493.0 * kClocksPerSample, 16777216.0);
    // step rounding (< 1/256 LSB per sample) accumulates at most n/256 LSBs.
    CHECK_NEAR(double(c.osc(0).acc24()), exact, double(n) / 256.0 + 1.0);
}

TEST(a4_saw_is_440hz_within_one_cent) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    c.write(kModeVol, 0x0F);
    voice(c, 0, 7493, 0x800, kSaw | kGate);   // 440 * 2^24 / 985248
    const auto x = render(c, 44100);
    const double hz = measureHz(x, 4410);
    const double cents = 1200.0 * std::log2(hz / 440.0);
    CHECK_NEAR(cents, 0.0, 1.0);
}

TEST(pulse_duty_follows_pulse_width) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    c.write(kModeVol, 0x0F);
    // The output is high while (accumulator >> 12) >= PW: 0x400 -> 75 % high.
    voice(c, 0, 2000, 0x400, kPulse | kGate);
    const auto x = render(c, 44100);
    int high = 0, n = 0;
    for (size_t i = 4410; i < x.size(); ++i, ++n) high += x[i] > 0.0f;
    CHECK_NEAR(double(high) / n, 0.75, 0.01);
}

TEST(attack_rate_0_takes_255_x_9_clocks) {
    Envelope e;
    e.writeAD(0x00);
    e.writeSR(0xF0);
    e.setGate(true);
    uint32_t clocks = 0;
    while (e.level() < 0xFF && clocks < 100000) {
        e.clock(1);
        ++clocks;
    }
    CHECK_EQ(clocks, 255u * 9u);
    CHECK(e.state() == Envelope::kDecaySustain);
}

TEST(attack_rate_2_takes_255_x_63_clocks_in_steps) {
    Envelope e;
    e.writeAD(0x20);
    e.setGate(true);
    e.clock(255u * 63u - 1u);
    CHECK_EQ(e.level(), 254);
    e.clock(1);
    CHECK_EQ(e.level(), 255);
}

TEST(release_0_follows_exponential_periods) {
    Envelope e;
    e.writeAD(0x00);
    e.writeSR(0xF0);
    e.setGate(true);
    e.clock(255u * 9u);
    CHECK_EQ(e.level(), 255);
    e.setGate(false);
    // 162 x 1 + 39 x 2 + 28 x 4 + 12 x 8 + 8 x 16 + 6 x 30 = 756 hits of 9 clocks.
    const uint32_t expected = 756u * 9u;
    e.clock(expected - 9u);
    CHECK_EQ(e.level(), 1);
    e.clock(9u);
    CHECK_EQ(e.level(), 0);
    e.clock(100000u);
    CHECK_EQ(e.level(), 0);   // frozen at zero
}

TEST(sustain_level_holds) {
    Envelope e;
    e.writeAD(0x00);
    e.writeSR(0x80);
    e.setGate(true);
    e.clock(200000u);
    CHECK_EQ(e.level(), 0x88);
}

TEST(adsr_delay_when_period_drops_below_counter) {
    Envelope e;
    e.writeAD(0xF0);   // attack period 31251
    e.setGate(true);
    e.clock(20000);
    CHECK_EQ(e.level(), 0);
    e.writeAD(0x00);   // period 9: the counter must wrap through 0x7FFF first
    e.clock(0x8000u - 20000u + 9u - 1u);
    CHECK_EQ(e.level(), 0);
    e.clock(1);
    CHECK_EQ(e.level(), 1);
}

TEST(noise_lfsr_taps_22_17_clocked_on_bit19) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    const uint16_t freq = 0x8123;
    voice(c, 0, freq, 0, kNoise | kGate);
    const uint64_t step = c.osc(0).step();
    const int n = 3000;
    render(c, n);
    // Reference: count bit-19 rising edges over the unwrapped phase.
    const uint64_t total = step * uint64_t(n);
    uint64_t edges = 0;
    if (total >= (1ull << 27)) edges = (total - (1ull << 27)) / (1ull << 28) + 1;
    uint32_t ref = 0x7FFFFF;
    for (uint64_t i = 0; i < edges; ++i) ref = ((ref << 1) | (((ref >> 22) ^ (ref >> 17)) & 1u)) & 0x7FFFFFu;
    CHECK(edges > 100);
    CHECK_EQ(c.osc(0).lfsr(), ref);
}

TEST(test_bit_holds_accumulator_and_resets_noise) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    voice(c, 0, 0x4000, 0, kNoise | kGate);
    render(c, 500);
    CHECK(c.osc(0).lfsr() != 0x7FFFFFu);
    c.write(kControl, kNoise | kTest | kGate);
    render(c, 500);
    CHECK_EQ(c.osc(0).acc24(), 0u);
    CHECK_EQ(c.osc(0).lfsr(), 0x7FFFFFu);
}

TEST(hard_sync_resets_voice1_on_voice3_msb) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    voice(c, 2, 1500, 0, kTri);                  // sync source, silent
    voice(c, 0, 5200, 0, kSaw | kSync | kGate);  // synced voice 1
    int resets = 0;
    float buf[1];
    for (int i = 0; i < 20000; ++i) {
        c.render(1, buf);
        if (c.osc(2).msbRose()) {
            ++resets;
            CHECK(c.osc(0).phase() <= c.osc(0).step());
        }
    }
    // 1500 * 2^-24 * 985248 Hz = 88.1 Hz -> about 40 resets in 20000 samples.
    CHECK(resets >= 38 && resets <= 41);
}

TEST(ring_mod_inverts_triangle_with_source_msb) {
    setup();
    Chip a, b;
    a.init(Model::k8580, kPalClockHz, kFs);
    b.init(Model::k8580, kPalClockHz, kFs);
    voice(a, 2, 900, 0, 0);
    voice(b, 2, 900, 0, 0);
    voice(a, 0, 3100, 0, kTri | kGate);
    voice(b, 0, 3100, 0, kTri | kRing | kGate);
    float buf[1];
    int differ = 0;
    for (int i = 0; i < 5000; ++i) {
        a.render(1, buf);
        b.render(1, buf);
        const bool srcMsb = (b.osc(2).phase() & 0x80000000u) != 0;
        if (srcMsb) {
            CHECK_EQ(b.lastWave(0), uint16_t(a.lastWave(0) ^ 0xFFF));
            ++differ;
        } else {
            CHECK_EQ(b.lastWave(0), a.lastWave(0));
        }
    }
    CHECK(differ > 1000);
}

TEST(combined_waveform_is_below_the_and) {
    setup();
    for (int m = 0; m < kModelCount; ++m) {
        const uint8_t* ts = comboTable(Model(m), kComboTS);
        for (uint32_t s = 0; s < 4096; ++s) {
            const uint16_t tri = uint16_t(((s & 0x800) ? (~s & 0x7FF) : (s & 0x7FF)) << 1);
            CHECK((uint16_t(ts[s] << 4) & ~(tri & s)) == 0);
        }
    }
}

TEST(lowpass_attenuates_and_models_differ) {
    setup();
    auto run = [](Model m, bool filtered) {
        Chip c;
        c.init(m, kPalClockHz, kFs);
        voice(c, 0, 0xF000, 0, kSaw | kGate);   // ~3.5 kHz saw
        c.write(kFcLo, 0);
        c.write(kFcHi, 0x10);
        c.write(kResFilt, filtered ? 0x01 : 0x00);
        c.write(kModeVol, uint8_t(kLP | 0x0F));
        return rms(render(c, 8820), 882);
    };
    const double dry = run(Model::k8580, false);
    const double wet8580 = run(Model::k8580, true);
    const double wet6581 = run(Model::k6581, true);
    CHECK(dry > 0.3);
    CHECK(wet8580 < dry * 0.1);
    CHECK(std::fabs(wet8580 - wet6581) > 0.001);
}

TEST(voice3_off_and_mute) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    voice(c, 2, 0x2000, 0, kSaw | kGate);
    c.write(kModeVol, uint8_t(k3Off | 0x0F));
    CHECK(rms(render(c, 4410), 441) < 1e-6);
    c.write(kModeVol, 0x0F);
    CHECK(rms(render(c, 4410), 441) > 0.1);
    c.setVoiceMuted(2, true);
    CHECK(rms(render(c, 4410), 441) < 1e-6);
}

TEST(osc3_and_env3_readback) {
    setup();
    Chip c;
    c.init(Model::k8580, kPalClockHz, kFs);
    voice(c, 2, 0x1000, 0, kSaw | kGate);
    render(c, 1000);
    CHECK_EQ(c.read(kEnv3), 0xFF);
    CHECK_EQ(c.read(kOsc3), uint8_t(c.lastWave(2) >> 4));
}
