// SIDTracker -- master effects (PLAN.md §13, M8): tempo-synced stereo delay,
// chorus, lightweight reverb, in that order, on the float stereo bus.
// Optional: everything off costs nothing; a global switch (Settings) turns
// them all off. Realtime rules: no allocation, fixed-size state; the delay
// line is external memory (SDRAM on the device, read and written
// sequentially), the chorus lines are small int16 arrays in SRAM. The
// reverb runs at half the sample rate on float lines in SRAM (K66: 2x
// cheaper than full rate, no int16 conversion or saturation per line).
#pragma once

#include <cstdint>

namespace sidt {
namespace audio {

// Song data (Extended features; not exported to GoatTracker).
struct FxSettings {
    uint8_t delayMix = 0;        // 0..100 % wet (0 = off)
    uint8_t delayTicks = 3;      // 1..64 tracker ticks
    uint8_t delayFeedback = 40;  // 0..90 %
    uint8_t chorusMix = 0;       // 0..100 % (0 = off)
    uint8_t chorusRate = 20;     // 1..100 (0.05..5 Hz)
    uint8_t chorusDepth = 40;    // 0..100
    uint8_t reverbMix = 0;       // 0..100 % (0 = off)
    uint8_t reverbSize = 60;     // 0..100
    uint8_t reverbDamp = 40;     // 0..100
    bool any() const { return delayMix || chorusMix || reverbMix; }
};

class Effects {
public:
    static constexpr uint32_t kChorusLen = 2048;   // per channel, 46 ms
    // Freeverb-style network: 4 combs + 2 all-passes per channel.
    static constexpr int kCombs = 4, kAllpasses = 2;

    // delayMem: interleaved stereo int16 ring of delayFrames frames (2 s at
    // 44.1 kHz = 88,200 frames). Null = no delay.
    void init(int16_t* delayMem, uint32_t delayFrames, uint32_t sampleRateMilliHz);
    // Block start (audio context): settings and the tick length.
    void configure(const FxSettings& s, uint32_t samplesPerTick);
    // In place on the bus.
    void process(float* L, float* R, int n);
    void clear();

private:
    void delay(float* L, float* R, int n);
    void chorus(float* L, float* R, int n);
    void reverb(float* L, float* R, int n);

    FxSettings s_;
    uint32_t fsHz_ = 44100;
    // Delay.
    int16_t* dMem_ = nullptr;
    uint32_t dFrames_ = 0, dPos_ = 0, dLen_ = 1;
    // Chorus.
    int16_t cBuf_[2][kChorusLen] = {};
    uint32_t cPos_ = 0;
    float cPhase_ = 0.0f;
    // Reverb.
    struct Comb {
        float* buf;
        uint16_t len, pos;
        float store;
    };
    struct Allpass {
        float* buf;
        uint16_t len, pos;
    };
    Comb comb_[2][kCombs] = {};
    Allpass ap_[2][kAllpasses] = {};
    // Freeverb lengths halved (half rate), right channel 12 longer.
    float rMem_[2 * (558 + 594 + 638 + 678 + 4 * 12) + 2 * (278 + 220 + 2 * 12)] = {};
    float rFeedback_ = 0.84f, rDamp_ = 0.2f;
    float rIn_ = 0.0f, rLast_[2] = {};   // half-rate input sum, last output
    bool rOdd_ = false;
    bool ready_ = false;
};

}  // namespace audio
}  // namespace sidt
