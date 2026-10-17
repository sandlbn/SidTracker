// SIDTracker -- SID oscillator: 24-bit phase accumulator, waveform
// generators and the noise LFSR.
//
// The accumulator is kept in the top 24 bits of a uint32 (8 fraction bits
// below the chip's LSB) and advances by freq x (SID clocks per sample) each
// output sample. Saw and pulse are band-limited with polyBLEP on their
// edges (also after a hard-sync reset, post-edge half only); triangle,
// noise and the combined waveforms are taken as the chip produces them.
#pragma once

#include <cstdint>

#include "sid/regs.h"
#include "sid/tables.h"

namespace sidt {
namespace sid {

class Oscillator {
public:
    void reset();
    // clocksPerSample24: SID clocks per output sample, 8.24 fixed point.
    void setFreq(uint16_t freq, uint32_t clocksPerSample24);
    void setPulseWidth(uint16_t pw12);
    void setControl(uint8_t ctrl);

    // Pass 1 (all voices of a chip): advance the accumulator, clock the noise
    // LFSR on bit-19 rising edges, note an MSB rising edge.
    void advance();
    // Pass 2: hard sync from `src` if this voice has SYNC set and the
    // source's MSB rose during this sample.
    void applySync(const Oscillator& src);
    // Pass 3: waveform output in -1..1 (and the 12-bit chip value for OSC3).
    float output(const Oscillator& ringSrc, Model model, uint16_t& w12);

    uint32_t phase() const { return phase_; }
    uint32_t acc24() const { return phase_ >> 8; }
    uint32_t step() const { return step_; }
    uint32_t lfsr() const { return lfsr_; }
    uint16_t freq() const { return freq_; }
    uint16_t pulseWidth() const { return pw_; }
    uint8_t control() const { return ctrl_; }
    bool msbRose() const { return msbRose_; }

private:
    void clockNoise();
    uint16_t noise12() const;

    uint32_t phase_ = 0;
    uint32_t step_ = 0;
    uint32_t lfsr_ = 0x7FFFFF;
    uint16_t freq_ = 0;
    uint16_t pw_ = 0;
    uint8_t ctrl_ = 0;
    bool msbRose_ = false;
    // Since the edge that set msbRose_ (fraction of this sample, 0..1).
    float edgeFrac_ = 0.0f;
    // A sync reset happened this sample: post-edge BLEP amplitudes for saw
    // and pulse (jump size / 2), applied once in output().
    bool synced_ = false;
    float syncSawJump_ = 0.0f, syncPulseJump_ = 0.0f;
};

}  // namespace sid
}  // namespace sidt

#include "sid/oscillator_impl.h"
