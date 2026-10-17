// OpenTracker -- Si4703 FM tuner on I2C0 implementing IRadio (spec 01 §5.6,
// spec 05 appendix C §2.8). Register model from the public Si4702/03 datasheet
// and AN230 (programming guide).
//
// The chip is held in reset (RST_N LOW, its I2C pins high-impedance) except
// while the recorder's radio source is selected. Power-up needs ~610 ms of
// waits (crystal 500 ms + power-up 110 ms, spec 01 §5.6); they run as a
// state machine from service() so the main loop never blocks. tune()/seek()
// issued while powering up are queued; busy() is true until they finish.
#pragma once

#include <stdint.h>
#include "hal_api.h"

namespace ot {
namespace hal {

class Si4703Radio final : public IRadio {
public:
    // Reset (2-wire mode) and identify the chip; starts the asynchronous
    // power-up. false = no answer on I2C0 (chip stays in reset).
    bool powerOn();
    // Power down and hold RST_N LOW (the selftest/boot state).
    void powerOff();
    bool powered() const { return state_ != kOff; }
    // Region index of the config "Radio region" (spec 05 config table:
    // 0 EU, 1 US, 2 AU, 3 JP). Selects de-emphasis; band/spacing come from
    // setBand(). Applied on the next power-up or setBand().
    void setRegion(uint8_t region) { region_ = region & 3; }
    // Last STATUSRSSI.RSSI (dBuV, 0..75), refreshed by service() about every
    // 500 ms while powered; -1 when off.
    int rssi() const { return state_ == kOff ? -1 : rssi_; }
    bool stereo() const { return stereo_; }
    uint16_t deviceId() const { return regs_[0]; }   // 0x1242 expected (datasheet DEVICEID)
    uint32_t errorCount() const { return errors_; }

    void service();   // main loop

    // IRadio
    void setBand(float minMHz, float maxMHz, float stepMHz) override;
    bool tune(float mhz) override;
    bool seek(bool up) override;
    bool busy() const override;
    float frequency() const override { return freq_; }

private:
    enum State : uint8_t { kOff, kOsc, kPowerUp, kReady, kTuning, kTuneEnd, kSeeking, kSeekEnd };
    bool readRegs(int count);   // regs 0x0A.. (wrapping); count of 16-bit regs
    bool writeRegs();           // regs 0x02..0x07 (12 bytes)
    void applyBandBits();
    uint16_t channelFor(float mhz) const;
    float freqFor(uint16_t chan) const;
    void startTune();
    void startSeek();
    void fail();

    uint16_t regs_[16] = {};
    State state_ = kOff;
    uint32_t t0_ = 0, lastStatus_ = 0;
    float bandMin_ = 87.5f, bandMax_ = 108.0f, step_ = 0.1f;
    float bottom_ = 87.5f;          // datasheet band bottom (87.5 or 76.0)
    float freq_ = 87.5f;
    float pendingFreq_ = 87.5f;
    bool pendingTune_ = false;
    int8_t pendingSeek_ = 0;        // +1 up, -1 down
    uint8_t region_ = 0;
    int rssi_ = 0;
    bool stereo_ = false;
    uint32_t errors_ = 0;
};

Si4703Radio& radio();

}  // namespace hal
}  // namespace ot
