// SIDTracker -- inline per-sample oscillator code (included by oscillator.h
// so the chip's render loop inlines it; no LTO in the device build).
#pragma once

namespace sidt {
namespace sid {

namespace detail {

constexpr float kPhaseScale = 1.0f / 4294967296.0f;   // 2^-32

// polyBLEP residuals for a unit step at t = 0 (t, dt in cycles). Post: the
// sample just after the edge; pre: the sample just before the next one.
inline float blepPost(float t, float dt) {
    if (t >= dt) return 0.0f;
    const float x = t / dt;
    return x + x - x * x - 1.0f;
}
inline float blepPre(float t, float dt) {
    if (t <= 1.0f - dt) return 0.0f;
    const float x = (t - 1.0f) / dt;
    return x * x + x + x + 1.0f;
}

}  // namespace detail

inline void Oscillator::clockNoise() {
    const uint32_t bit = ((lfsr_ >> 22) ^ (lfsr_ >> 17)) & 1u;
    lfsr_ = ((lfsr_ << 1) | bit) & 0x7FFFFFu;
}

inline uint16_t Oscillator::noise12() const {
    const uint32_t r = lfsr_;
    return uint16_t((((r >> 20) & 1) << 11) | (((r >> 18) & 1) << 10) | (((r >> 14) & 1) << 9) |
                    (((r >> 11) & 1) << 8) | (((r >> 9) & 1) << 7) | (((r >> 5) & 1) << 6) |
                    (((r >> 2) & 1) << 5) | ((r & 1) << 4));
}

inline void Oscillator::advance() {
    synced_ = false;
    if (ctrl_ & kTest) {
        msbRose_ = false;
        return;
    }
    const uint32_t prev = phase_;
    phase_ = prev + step_;
    msbRose_ = !(prev & 0x80000000u) && (phase_ & 0x80000000u);
    if (msbRose_) edgeFrac_ = step_ ? float(phase_ - 0x80000000u) / float(step_) : 0.0f;
    // Noise is clocked on rising edges of accumulator bit 19 (phase bit 27):
    // count the multiples of 2^28 crossed by the phase shifted by 2^27.
    const uint64_t a = uint32_t(prev - (1u << 27));
    const uint64_t b = a + step_;
    uint32_t edges = uint32_t(b >> 28) - uint32_t(a >> 28);   // <= 2 (step < 2^29)
    while (edges--) clockNoise();
}

inline void Oscillator::applySync(const Oscillator& src) {
    if (!(ctrl_ & kSync) || !src.msbRose_ || (ctrl_ & kTest)) return;
    // Where this oscillator was when the source's MSB rose, then restart
    // from zero for the remaining part of the sample.
    const uint32_t since = uint32_t(float(step_) * src.edgeFrac_);
    const uint32_t atEdge = phase_ - since;
    const float tEdge = float(atEdge) * detail::kPhaseScale;
    syncSawJump_ = tEdge;                                // (2t - 1) - (-1), halved
    const bool highBefore = (atEdge >> 20) >= pw_;
    const bool highAfter = pw_ == 0;
    syncPulseJump_ = (float(highBefore) - float(highAfter));
    phase_ = since;
    synced_ = true;
}

__attribute__((always_inline)) inline float Oscillator::output(const Oscillator& ringSrc, Model model, uint16_t& w12) {
    const uint8_t wave = uint8_t(ctrl_ >> 4);
    const uint16_t saw12 = uint16_t(phase_ >> 20);
    const bool test = (ctrl_ & kTest) != 0;
    const bool pulseHigh = test || saw12 >= pw_;
    const float t = float(phase_) * detail::kPhaseScale;
    const float dt = float(step_) * detail::kPhaseScale;
    const bool blep = !test && step_ != 0;
    switch (wave) {
        case 0:
            w12 = 0;   // floating DAC: treated as silence (no DC)
            return 0.0f;
        case 1: {
            bool msb = (phase_ & 0x80000000u) != 0;
            if ((ctrl_ & kRing) && (ringSrc.phase_ & 0x80000000u)) msb = !msb;
            uint16_t tri = uint16_t((phase_ >> 19) & 0xFFF);
            if (msb) tri ^= 0xFFF;
            w12 = tri;
            return float(tri) * (2.0f / 4095.0f) - 1.0f;
        }
        case 2: {
            w12 = saw12;
            float s = 2.0f * t - 1.0f;
            if (blep) {
                s -= synced_ ? syncSawJump_ * detail::blepPost(t, dt) : detail::blepPost(t, dt);
                s -= detail::blepPre(t, dt);
            }
            return s;
        }
        case 4: {
            w12 = pulseHigh ? 0xFFF : 0;
            float p = pulseHigh ? 1.0f : -1.0f;
            if (blep && pw_ != 0) {
                // Falling edge at the wrap, rising edge where saw crosses pw.
                p -= synced_ ? syncPulseJump_ * detail::blepPost(t, dt) : detail::blepPost(t, dt);
                p -= detail::blepPre(t, dt);
                float tr = t - float(pw_) * (1.0f / 4096.0f);
                if (tr < 0.0f) tr += 1.0f;
                p += detail::blepPost(tr, dt) + detail::blepPre(tr, dt);
            }
            return p;
        }
        case 8:
            w12 = noise12();
            return float(w12) * (2.0f / 4095.0f) - 1.0f;
        case 3:
            w12 = uint16_t(comboTable(model, kComboTS)[saw12] << 4);
            break;
        case 5:
            w12 = pulseHigh ? uint16_t(comboTable(model, kComboPT)[saw12] << 4) : 0;
            break;
        case 6:
            w12 = pulseHigh ? uint16_t(comboTable(model, kComboPS)[saw12] << 4) : 0;
            break;
        case 7:
            w12 = pulseHigh ? uint16_t(comboTable(model, kComboPST)[saw12] << 4) : 0;
            break;
        default: {
            // Noise combined with anything: AND of the outputs (the chip's
            // shift-register lock-up is not modelled).
            uint16_t v = noise12();
            if (wave & 1) {
                uint16_t tri = uint16_t((phase_ >> 19) & 0xFFF);
                if (phase_ & 0x80000000u) tri ^= 0xFFF;
                v &= tri;
            }
            if (wave & 2) v &= saw12;
            if ((wave & 4) && !pulseHigh) v = 0;
            w12 = v;
            break;
        }
    }
    return float(w12) * (2.0f / 4095.0f) - 1.0f;
}

}  // namespace sid
}  // namespace sidt
