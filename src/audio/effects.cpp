// SIDTracker -- master effects (effects.h).
#include "audio/effects.h"

#include <cstring>

namespace sidt {
namespace audio {

namespace {
constexpr float kToI16 = 32767.0f, kFromI16 = 1.0f / 32767.0f;
inline int16_t sat16(float v) {
    const float x = v * kToI16;
    return int16_t(x > 32767.0f ? 32767 : (x < -32767.0f ? -32767 : int32_t(x)));
}
// Freeverb's comb / all-pass lengths at 44.1 kHz halved for the half-rate
// network; the right channel is 12 samples longer for stereo width.
const uint16_t kCombLen[Effects::kCombs] = {558, 594, 638, 678};
const uint16_t kApLen[Effects::kAllpasses] = {278, 220};
constexpr uint16_t kSpread = 12;
}  // namespace

void Effects::init(int16_t* delayMem, uint32_t delayFrames, uint32_t sampleRateMilliHz) {
    dMem_ = delayMem;
    dFrames_ = delayMem ? delayFrames : 0;
    fsHz_ = sampleRateMilliHz / 1000;
    float* p = rMem_;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < kCombs; ++i) {
            comb_[ch][i] = Comb{p, uint16_t(kCombLen[i] + ch * kSpread), 0, 0.0f};
            p += comb_[ch][i].len;
        }
        for (int i = 0; i < kAllpasses; ++i) {
            ap_[ch][i] = Allpass{p, uint16_t(kApLen[i] + ch * kSpread), 0};
            p += ap_[ch][i].len;
        }
    }
    ready_ = true;
    clear();
}

void Effects::clear() {
    if (dMem_) std::memset(dMem_, 0, size_t(dFrames_) * 2 * sizeof(int16_t));
    std::memset(cBuf_, 0, sizeof cBuf_);
    std::memset(rMem_, 0, sizeof rMem_);
    for (auto& ch : comb_)
        for (Comb& c : ch) c.store = 0.0f;
    rIn_ = rLast_[0] = rLast_[1] = 0.0f;
    rOdd_ = false;
}

void Effects::configure(const FxSettings& s, uint32_t samplesPerTick) {
    s_ = s;
    if (dFrames_) {
        uint32_t len = uint32_t(s.delayTicks ? s.delayTicks : 1) * samplesPerTick;
        dLen_ = len < 1 ? 1 : (len > dFrames_ ? dFrames_ : len);
    }
    rFeedback_ = 0.70f + 0.28f * float(s.reverbSize) / 100.0f;
    rDamp_ = 0.4f * float(s.reverbDamp) / 100.0f;
}

void Effects::process(float* L, float* R, int n) {
    if (!ready_) return;
    if (s_.delayMix && dFrames_) delay(L, R, n);
    if (s_.chorusMix) chorus(L, R, n);
    if (s_.reverbMix) reverb(L, R, n);
}

void Effects::delay(float* L, float* R, int n) {
    // Stereo delay with cross feedback (ping-pong) of dLen_ frames.
    const float wet = float(s_.delayMix) / 100.0f, fb = float(s_.delayFeedback) / 100.0f;
    uint32_t pos = dPos_;
    for (int i = 0; i < n; ++i) {
        uint32_t rd = pos + dFrames_ - dLen_;
        if (rd >= dFrames_) rd -= dFrames_;
        const float dl = float(dMem_[2 * rd]) * kFromI16, dr = float(dMem_[2 * rd + 1]) * kFromI16;
        dMem_[2 * pos] = sat16(L[i] + dr * fb);
        dMem_[2 * pos + 1] = sat16(R[i] + dl * fb);
        L[i] += dl * wet;
        R[i] += dr * wet;
        if (++pos >= dFrames_) pos = 0;
    }
    dPos_ = pos;
}

void Effects::chorus(float* L, float* R, int n) {
    // One modulated tap per channel (90 degrees apart), 5..23 ms, triangle LFO.
    const float wet = float(s_.chorusMix) / 100.0f;
    const float rateHz = 0.05f + 4.95f * float(s_.chorusRate) / 100.0f;
    const float inc = rateHz / float(fsHz_);
    const float base = 0.005f * float(fsHz_);
    const float depth = 0.018f * float(fsHz_) * float(s_.chorusDepth) / 100.0f;
    uint32_t pos = cPos_;
    float ph = cPhase_;
    for (int i = 0; i < n; ++i) {
        cBuf_[0][pos] = sat16(L[i]);
        cBuf_[1][pos] = sat16(R[i]);
        for (int ch = 0; ch < 2; ++ch) {
            float p = ph + 0.25f * float(ch);
            if (p >= 1.0f) p -= 1.0f;
            const float tri = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;   // 0..1
            const float d = base + depth * tri;
            const uint32_t di = uint32_t(d);
            const float fr = d - float(di);
            const uint32_t a = (pos + kChorusLen - di) & (kChorusLen - 1);
            const uint32_t b = (a + kChorusLen - 1) & (kChorusLen - 1);
            const float v = (float(cBuf_[ch][a]) * (1.0f - fr) + float(cBuf_[ch][b]) * fr) * kFromI16;
            if (ch == 0) L[i] = L[i] * (1.0f - 0.5f * wet) + v * wet;
            else R[i] = R[i] * (1.0f - 0.5f * wet) + v * wet;
        }
        pos = (pos + 1) & (kChorusLen - 1);
        ph += inc;
        if (ph >= 1.0f) ph -= 1.0f;
    }
    cPos_ = pos;
    cPhase_ = ph;
}

void Effects::reverb(float* L, float* R, int n) {
    // Half rate: the even sample's input is kept, the odd one runs the
    // network on the pair's sum; the output is the last result, then the
    // mid point to the new one (linear interpolation, half a step late).
    const float wet = float(s_.reverbMix) / 100.0f * 3.0f;
    const float fb = rFeedback_, damp = rDamp_, damp1 = 1.0f - damp;
    float in = rIn_, last0 = rLast_[0], last1 = rLast_[1];
    bool odd = rOdd_;
    for (int i = 0; i < n; ++i) {
        const float x = (L[i] + R[i]) * 0.0075f;
        if (!odd) {
            in = x;
            L[i] += last0 * wet;
            R[i] += last1 * wet;
        } else {
            in += x;
            float out[2];
            for (int ch = 0; ch < 2; ++ch) {
                float acc = 0.0f;
                for (Comb& c : comb_[ch]) {
                    const float y = c.buf[c.pos];
                    c.store = y * damp1 + c.store * damp;
                    c.buf[c.pos] = in + c.store * fb;
                    if (++c.pos >= c.len) c.pos = 0;
                    acc += y;
                }
                for (Allpass& a : ap_[ch]) {
                    const float b = a.buf[a.pos];
                    a.buf[a.pos] = acc + b * 0.5f;
                    acc = b - acc;
                    if (++a.pos >= a.len) a.pos = 0;
                }
                out[ch] = acc;
            }
            L[i] += 0.5f * (last0 + out[0]) * wet;
            R[i] += 0.5f * (last1 + out[1]) * wet;
            last0 = out[0];
            last1 = out[1];
        }
        odd = !odd;
    }
    rIn_ = in;
    rLast_[0] = last0;
    rLast_[1] = last1;
    rOdd_ = odd;
}

}  // namespace audio
}  // namespace sidt
