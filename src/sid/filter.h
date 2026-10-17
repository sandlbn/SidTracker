// SIDTracker -- SID multimode filter: a topology-preserving (TPT) state
// variable filter, stable up to Nyquist, with per-model cutoff curve and
// resonance range, plus a cheap saturation stage for the 6581.
#pragma once

#include <cstdint>

#include "sid/tables.h"

namespace sidt {
namespace sid {

class Filter {
public:
    void reset();
    void setModel(Model m);
    void setCutoff(uint16_t fc11);          // 11-bit FC register value
    void setResonance(uint8_t res4);
    void setMode(uint8_t modeBits);         // kLP | kBP | kHP (MODE/VOL bits 4..6)

    // One sample: returns the sum of the selected outputs.
    float process(float x) {
        const float hp = (x - k_ * s1_ - g_ * s1_ - s2_) * h_;
        const float v1 = g_ * hp;
        float bp = v1 + s1_;
        s1_ = bp + v1;
        const float v2 = g_ * bp;
        const float lp = v2 + s2_;
        s2_ = lp + v2;
        if (saturate_) {
            // Headroom of about two full voices before the states bend.
            s1_ = 2.0f * soft(0.5f * s1_);
            s2_ = 2.0f * soft(0.5f * s2_);
        }
        return lp * lpOn_ + bp * bpOn_ + hp * hpOn_;
    }

    uint16_t cutoff() const { return fc_; }
    uint8_t resonance() const { return res_; }
    uint8_t mode() const { return mode_; }

private:
    void update();
    // tanh-like rational soft clip, exact enough for |x| < 3, clamped beyond.
    static float soft(float x) {
        if (x > 3.0f) return 1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    float s1_ = 0.0f, s2_ = 0.0f;
    float g_ = 0.1f, k_ = 1.414f, h_ = 1.0f;
    float lpOn_ = 0.0f, bpOn_ = 0.0f, hpOn_ = 0.0f;
    uint16_t fc_ = 0;
    uint8_t res_ = 0, mode_ = 0;
    Model model_ = Model::k8580;
    bool saturate_ = false;
};

}  // namespace sid
}  // namespace sidt
