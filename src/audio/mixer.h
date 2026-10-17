// SIDTracker -- six-voice output mixer (PLAN.md §4 "Mixer"): two SID chip
// outputs -> AC coupling, SID balance -> a float stereo bus (1.0 = full
// scale) on which effects and PCM work -> master gain, clipping, int16 out.
#pragma once

#include <cstdint>

namespace sidt {
namespace audio {

class Mixer {
public:
    // SID balance (PLAN.md §10): 0 = both chips centred, 100 = SID1 hard
    // left / SID2 hard right.
    void setBalance(uint8_t width0to100);
    uint8_t balance() const { return balance_; }
    void setMasterGain(float g) { master_ = g; }   // 0..1
    void reset();

    // a, b: chip outputs (n mono samples each) -> the stereo bus L, R.
    void mixToBus(const float* a, const float* b, int n, float* L, float* R);
    // Adds a chip's individually panned voices (already stereo) to the bus.
    void addPanned(int chip, const float* l, const float* r, int n, float* L, float* R);
    // Bus -> n interleaved int16 L/R frames (master gain, clipping, peak).
    void toOutput(const float* L, const float* R, int n, int16_t* out);
    void mix(const float* a, const float* b, int n, int16_t* out) {   // without a bus stage
        float L[128], R[128];
        mixToBus(a, b, n, L, R);
        toOutput(L, R, n, out);
    }

    // Peak |sample| of the last mix call, 0..32767 (meters, clip detection).
    int16_t peak() const { return peak_; }
    // Peak of each chip's contribution to the bus in the last block (bus
    // units, 1.0 = full scale; meters).
    float chipPeak(int chip) const { return chipPeak_[chip & 1]; }
    uint32_t clips() const { return clips_; }

private:
    // Per-chip scale on the bus: three full voices at volume 15 reach about
    // -6 dBFS.
    static constexpr float kChipGain = 0.17f;
    // C64 audio output is AC coupled (about 16 Hz high-pass).
    static constexpr float kDcPole = 0.99772f;
    float gl_[2] = {1.0f, 1.0f}, gr_[2] = {1.0f, 1.0f};
    float dcX_[2] = {0, 0}, dcY_[2] = {0, 0};
    float pdcX_[2][2] = {}, pdcY_[2][2] = {};   // panned voices: chip x side
    float master_ = 0.8f;
    uint8_t balance_ = 0;
    int16_t peak_ = 0;
    float chipPeak_[2] = {};
    uint32_t clips_ = 0;
};

}  // namespace audio
}  // namespace sidt
