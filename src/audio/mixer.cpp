// SIDTracker -- output mixer (mixer.h).
#include "audio/mixer.h"

namespace sidt {
namespace audio {

void Mixer::setBalance(uint8_t width) {
    if (width > 100) width = 100;
    balance_ = width;
    // Linear pan, centre = unity on both sides.
    const float p = float(width) / 100.0f;   // SID1 pan = -p, SID2 pan = +p
    gl_[0] = 1.0f;
    gr_[0] = 1.0f - p;
    gl_[1] = 1.0f - p;
    gr_[1] = 1.0f;
}

void Mixer::reset() {
    for (int c = 0; c < 2; ++c) dcX_[c] = dcY_[c] = 0.0f;
    peak_ = 0;
}

void Mixer::mixToBus(const float* a, const float* b, int n, float* L, float* R) {
    const float g = kChipGain;
    const float l0 = gl_[0] * g, r0 = gr_[0] * g, l1 = gl_[1] * g, r1 = gr_[1] * g;
    float x0 = dcX_[0], y0 = dcY_[0], x1 = dcX_[1], y1 = dcY_[1];
    float m0 = 0.0f, m1 = 0.0f;
    for (int i = 0; i < n; ++i) {
        y0 = a[i] - x0 + kDcPole * y0;
        x0 = a[i];
        y1 = b[i] - x1 + kDcPole * y1;
        x1 = b[i];
        L[i] = y0 * l0 + y1 * l1;
        R[i] = y0 * r0 + y1 * r1;
        const float a0 = y0 < 0.0f ? -y0 : y0, a1 = y1 < 0.0f ? -y1 : y1;
        if (a0 > m0) m0 = a0;
        if (a1 > m1) m1 = a1;
    }
    chipPeak_[0] = m0 * g;
    chipPeak_[1] = m1 * g;
    dcX_[0] = x0;
    dcY_[0] = y0;
    dcX_[1] = x1;
    dcY_[1] = y1;
}

void Mixer::addPanned(int chip, const float* l, const float* r, int n, float* L, float* R) {
    float* x = pdcX_[chip & 1];
    float* y = pdcY_[chip & 1];
    float m = 0.0f;
    for (int i = 0; i < n; ++i) {
        y[0] = l[i] - x[0] + kDcPole * y[0];
        x[0] = l[i];
        y[1] = r[i] - x[1] + kDcPole * y[1];
        x[1] = r[i];
        L[i] += y[0] * kChipGain;
        R[i] += y[1] * kChipGain;
        const float a0 = y[0] < 0.0f ? -y[0] : y[0], a1 = y[1] < 0.0f ? -y[1] : y[1];
        if (a0 > m) m = a0;
        if (a1 > m) m = a1;
    }
    m *= kChipGain;
    if (m > chipPeak_[chip & 1]) chipPeak_[chip & 1] = m;
}

void Mixer::toOutput(const float* L, const float* R, int n, int16_t* out) {
    const float g = 32767.0f * master_;
    int32_t peak = 0;
    for (int i = 0; i < n; ++i) {
        int32_t l = int32_t(L[i] * g);
        int32_t r = int32_t(R[i] * g);
        if (l > 32767 || l < -32767 || r > 32767 || r < -32767) ++clips_;
        if (l > 32767) l = 32767;
        if (l < -32767) l = -32767;
        if (r > 32767) r = 32767;
        if (r < -32767) r = -32767;
        const int32_t al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        const int32_t m = al > ar ? al : ar;
        if (m > peak) peak = m;
        out[2 * i] = int16_t(l);
        out[2 * i + 1] = int16_t(r);
    }
    peak_ = int16_t(peak);
}

}  // namespace audio
}  // namespace sidt
