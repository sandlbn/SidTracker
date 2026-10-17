// SIDTracker -- SID constants and generated tables (tables.h).
#include "sid/tables.h"

#include <cmath>

namespace sidt {
namespace sid {

const uint16_t kRatePeriod[16] = {
    9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251,
};

namespace {

uint8_t gCombo[kModelCount][kComboCount][4096];
float gCutoffG[kModelCount][2048];
bool gReady = false;

// 12-bit triangle from a 12-bit saw value (MSB folds the ramp).
uint16_t triFromSaw(uint16_t s) {
    const uint16_t t = (s & 0x800) ? uint16_t(~s & 0x7FF) : uint16_t(s & 0x7FF);
    return uint16_t(t << 1);
}

// Our combined-waveform model: the waveform DACs are wired together, so a
// bit only reads high when its neighbours in the AND are high too. Each
// output bit is the weighted vote of bits i-2..i+2 of the AND; the 6581
// pulls harder (thinner sound) than the 8580.
uint8_t combine(uint16_t a, Model m) {
    const float w[5] = {0.25f, 0.6f, 1.0f, 0.6f, 0.25f};
    const float thr = m == Model::k6581 ? 2.0f : 1.6f;
    uint16_t out = 0;
    for (int i = 0; i < 12; ++i) {
        float sum = 0.0f;
        for (int j = -2; j <= 2; ++j) {
            const int b = i + j;
            if (b < 0 || b > 11) continue;
            if ((a >> b) & 1) sum += w[j + 2];
        }
        if (((a >> i) & 1) && sum >= thr) out |= uint16_t(1u << i);
    }
    return uint8_t(out >> 4);
}

}  // namespace

const uint8_t* comboTable(Model m, Combo c) { return gCombo[int(m)][c]; }
const float* cutoffG(Model m) { return gCutoffG[int(m)]; }
bool tablesReady() { return gReady; }

float cutoffHz(Model m, uint16_t fc) {
    const float x = float(fc & 0x7FF) / 2047.0f;
    if (m == Model::k8580) return 30.0f + x * 12000.0f;
    // 6581: flat floor, then a steep exponential rise towards the top.
    const float k = 4.5f;
    return 220.0f + 17800.0f * (std::exp(k * x) - 1.0f) / (std::exp(k) - 1.0f);
}

float resonanceK(Model m, uint8_t res) {
    const float r = float(res & 15) / 15.0f;
    const float q = 0.707f + r * (m == Model::k6581 ? 2.6f : 2.0f);
    return 1.0f / q;
}

void initTables(uint32_t sampleRateMilliHz) {
    const float fs = float(sampleRateMilliHz) / 1000.0f;
    for (int mi = 0; mi < kModelCount; ++mi) {
        const Model m = Model(mi);
        for (uint32_t s = 0; s < 4096; ++s) {
            const uint16_t saw = uint16_t(s);
            const uint16_t tri = triFromSaw(saw);
            gCombo[mi][kComboTS][s] = combine(uint16_t(tri & saw), m);
            gCombo[mi][kComboPT][s] = combine(tri, m);
            gCombo[mi][kComboPS][s] = combine(saw, m);
            gCombo[mi][kComboPST][s] = combine(uint16_t(tri & saw), m);
        }
        for (int fc = 0; fc < 2048; ++fc) {
            float hz = cutoffHz(m, uint16_t(fc));
            if (hz > 0.45f * fs) hz = 0.45f * fs;
            gCutoffG[mi][fc] = std::tan(3.14159265f * hz / fs);
        }
    }
    gReady = true;
}

}  // namespace sid
}  // namespace sidt
