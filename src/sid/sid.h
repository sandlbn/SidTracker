// SIDTracker -- one emulated SID chip (APPROACH.md §3).
//
// Driven by register writes like the real chip; rendered per output sample at
// the codec rate with band-limited saw and pulse. Integer state (24-bit
// accumulators, noise LFSR, ADSR rate and exponential counters) advances in
// SID-clock units so sync, ring modulation, noise and envelope timing keep
// chip behaviour; the analogue side (waveform levels, filter, mix) is float,
// which costs the same as fixed point on the M4F's single-precision FPU.
//
// Realtime rules (PLAN.md §20): no allocation, no unbounded loops; every
// per-sample loop is bounded by the clocks per sample (<= 24).
#pragma once

#include <cstdint>

#include "sid/envelope.h"
#include "sid/filter.h"
#include "sid/oscillator.h"
#include "sid/regs.h"
#include "sid/tables.h"

namespace sidt {
namespace sid {

constexpr int kVoices = 3;
constexpr int kMaxRender = 128;   // samples per render call (one audio block)

class Chip {
public:
    // sampleRateMilliHz: the real output rate (device 44,117,647 = MCLK/256;
    // host 44,100,000), so pitch is exact without a compensated note table.
    void init(Model model, uint32_t clockHz, uint32_t sampleRateMilliHz);
    void reset();                         // registers 0, envelopes released to 0
    void setModel(Model m);
    Model model() const { return model_; }
    void setClock(uint32_t clockHz);      // PAL 985,248 / NTSC 1,022,727
    uint32_t clockHz() const { return clockHz_; }

    void write(uint8_t reg, uint8_t val);
    uint8_t shadow(uint8_t reg) const { return reg < kRegCount ? regs_[reg] : 0; }
    // Readable registers (OSC3, ENV3) as the chip would return them.
    uint8_t read(uint8_t reg) const;

    // Muted voices keep running (they still sync / ring-modulate others) but
    // contribute nothing to the mix (Perform mute/solo, PLAN.md §10).
    void setVoiceMuted(int v, bool muted) { if (v >= 0 && v < kVoices) muted_[v] = muted; }
    // Per-voice level 0..1 (Master mixer), applied before the filter.
    void setVoiceGain(int v, float g) { if (v >= 0 && v < kVoices) gain_[v] = g; }
    bool voiceMuted(int v) const { return v >= 0 && v < kVoices && muted_[v]; }
    // Per-voice stereo (M8): a panned voice leaves the chip's mono direct
    // path for pannedL/R (gains gl, gr); filtered voices stay with the chip.
    void setVoicePan(int v, bool on, float gl, float gr) {
        if (v < 0 || v >= kVoices) return;
        panOn_[v] = on;
        panGain_[v][0] = gl;
        panGain_[v][1] = gr;
        anyPan_ = panOn_[0] || panOn_[1] || panOn_[2];
    }
    bool anyPanned() const { return anyPan_; }
    const float* pannedL() const { return panL_; }
    const float* pannedR() const { return panR_; }

    // Two passes so the profiler can time voices and filter separately
    // (PLAN.md §6). renderVoices fills the internal filter-input and direct
    // buffers for n <= kMaxRender samples; renderFilter mixes them into out.
    void renderVoices(int n);
    void renderFilter(int n, float* out);
    void render(int n, float* out) {
        renderVoices(n);
        renderFilter(n, out);
    }

    // Envelope level 0..255 and waveform output 0..4095 of voice v (meters).
    uint8_t envLevel(int v) const { return voices_[v].env.level(); }
    uint16_t lastWave(int v) const { return lastWave_[v]; }
    const Oscillator& osc(int v) const { return voices_[v].osc; }
    const Filter& filter() const { return filter_; }

private:
    struct Voice {
        Oscillator osc;
        Envelope env;
    };
    void updateClockStep();

    Voice voices_[kVoices];
    Filter filter_;
    Model model_ = Model::k8580;
    uint32_t clockHz_ = kPalClockHz;
    uint32_t sampleRateMilliHz_ = 44100000;
    // SID clocks per output sample: 16.16 for the envelope clock (with its
    // fraction carry), 8.24 for the oscillator step (pitch precision).
    uint32_t clocksPerSample16_ = 0;
    uint32_t clocksPerSample24_ = 0;
    uint32_t clockFrac_ = 0;
    uint8_t regs_[kRegCount] = {};
    bool muted_[kVoices] = {};
    float gain_[kVoices] = {1.0f, 1.0f, 1.0f};
    uint16_t lastWave_[kVoices] = {};
    float volume_ = 0.0f;                // MODE/VOL volume / 15
    bool panOn_[kVoices] = {};
    bool anyPan_ = false;
    float panGain_[kVoices][2] = {};
    float panL_[kMaxRender] = {}, panR_[kMaxRender] = {};
    float filtIn_[kMaxRender] = {};
    float direct_[kMaxRender] = {};
};

}  // namespace sid
}  // namespace sidt
