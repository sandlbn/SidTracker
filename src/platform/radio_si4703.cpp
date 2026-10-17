// OpenTracker -- Si4703 FM tuner (spec 01 §5.6). Register names and bits from
// the public Si4702/03-C19 datasheet and Silicon Labs AN230.
#include "radio_si4703.h"

#include <Arduino.h>
#include "board.h"
#include "i2c_bus.h"
#include "k66.h"

namespace ot {
namespace hal {

namespace {
// Register indices (datasheet §6).
constexpr int POWERCFG = 0x02, CHANNEL = 0x03, SYSCONFIG1 = 0x04,
    SYSCONFIG2 = 0x05, SYSCONFIG3 = 0x06, TEST1 = 0x07, STATUSRSSI = 0x0A, READCHAN = 0x0B;
// POWERCFG bits.
constexpr uint16_t PWR_DSMUTE = 0x8000, PWR_DMUTE = 0x4000, PWR_MONO = 0x2000, PWR_RDSM = 0x0800,
    PWR_SKMODE = 0x0400, PWR_SEEKUP = 0x0200, PWR_SEEK = 0x0100, PWR_DISABLE = 0x0040, PWR_ENABLE = 0x0001;
constexpr uint16_t CH_TUNE = 0x8000;
constexpr uint16_t SC1_DE = 0x0800;            // 1 = 50 us de-emphasis, 0 = 75 us
constexpr uint16_t ST_STC = 0x4000, ST_ST = 0x0100;
// spec 01 §5.6 init values.
constexpr uint16_t kTest1Xosc = 0x8100;        // XOSCEN (+ AN230 reserved bit 8)
constexpr uint16_t kPowerCfg = PWR_DSMUTE | PWR_DMUTE | PWR_MONO | PWR_RDSM | PWR_ENABLE;   // 0xE801
static_assert(kPowerCfg == 0xE801, "spec 01 §5.6 POWERCFG");
constexpr uint16_t kSysCfg2Base = 0x190A;      // SEEKTH 0x19, VOLUME 10 (spec 01 §5.6)
constexpr uint16_t kSysCfg3Seek = 0x0048;      // SKSNR 4, SKCNT 8 (spec 01 §5.6)
constexpr uint32_t kOscMs = 500, kPowerUpMs = 110;   // spec 01 §5.6 / datasheet
constexpr uint32_t kTuneTimeoutMs = 200;       // datasheet: tune <= 60 ms
constexpr uint32_t kSeekTimeoutMs = 15000;     // full band at ~60 ms/channel
constexpr uint32_t kStcClearTimeoutMs = 50;
}  // namespace

Si4703Radio& radio() {
    static Si4703Radio r;
    return r;
}

bool Si4703Radio::readRegs(int count) {
    // Reads always start at 0x0A and wrap after 0x0F (datasheet "2-wire").
    uint8_t b[32];
    if (count > 16) count = 16;
    if (!i2c0().read(board::kAddrSi4703, b, uint32_t(count) * 2)) {
        errors_++;
        return false;
    }
    for (int i = 0; i < count; ++i) regs_[(STATUSRSSI + i) & 15] = uint16_t((b[2 * i] << 8) | b[2 * i + 1]);
    return true;
}

bool Si4703Radio::writeRegs() {
    // Writes always start at 0x02 (spec 01 §5.6: 12 bytes for regs 0x02-0x07).
    uint8_t b[12];
    for (int i = 0; i < 6; ++i) {
        b[2 * i] = uint8_t(regs_[POWERCFG + i] >> 8);
        b[2 * i + 1] = uint8_t(regs_[POWERCFG + i]);
    }
    if (!i2c0().write(board::kAddrSi4703, b, sizeof(b))) {
        errors_++;
        return false;
    }
    return true;
}

void Si4703Radio::fail() {
    // spec 01 §5.6: on I2C failure the stock driver resets and re-inits; we
    // drop to off and let the next selectInput() retry.
    powerOff();
}

bool Si4703Radio::powerOn() {
    if (state_ != kOff) return true;
    // spec 01 §5.6 reset: SDIO (PTD9 = I2C0 SDA) LOW while RST_N rises selects
    // the 2-wire bus mode (datasheet "Reset, Powerup and Powerdown"; SEN is
    // pulled up on the chip). Only called from the main loop, so no other
    // I2C0 transfer (codec, button keypad) can be in flight.
    gpioOutput(board::kPinRadioReset, false);
    delay(1);
    gpioOutput(board::kI2c0Sda, false);
    delay(1);
    gpioWrite(board::kPinRadioReset, true);
    delay(1);
    gpio(board::kI2c0Sda).PDDR &= ~(1u << board::kI2c0Sda.bit);
    i2c0().recover();   // spec 01 §5.6 "I2C0 then re-initialised"
    if (!readRegs(16)) {
        gpioWrite(board::kPinRadioReset, false);
        return false;
    }
    // UNVERIFIED: identity check only reported (deviceId()); 0x1242 = Si4702/03.
    regs_[TEST1] = kTest1Xosc;
    if (!writeRegs()) {
        gpioWrite(board::kPinRadioReset, false);
        return false;
    }
    state_ = kOsc;
    t0_ = millis();
    rssi_ = 0;
    return true;
}

void Si4703Radio::powerOff() {
    if (state_ != kOff) {
        regs_[POWERCFG] = PWR_DISABLE | PWR_ENABLE;   // datasheet powerdown
        writeRegs();
    }
    state_ = kOff;
    pendingSeek_ = 0;
    // Back to the boot state: RST_N LOW, chip pins high-impedance (boardEarlyInit).
    gpioOutput(board::kPinRadioReset, false);
}

void Si4703Radio::applyBandBits() {
    // Band (datasheet SYSCONFIG2[7:6]): 00 = 87.5-108, 01 = 76-108 (Japan wide).
    // Spacing [5:4]: 00 = 200 kHz, 01 = 100 kHz, 10 = 50 kHz. spec 05 appendix C
    // §2.8 region table: 87.5/0.1, 87.5/0.2, 87.5/0.2, 76.0/0.1 (UI max 108).
    uint16_t band = bandMin_ < 87.0f ? 1 : 0;
    bottom_ = band ? 76.0f : 87.5f;
    uint16_t space = step_ > 0.15f ? 0 : (step_ > 0.075f ? 1 : 2);
    regs_[SYSCONFIG2] = uint16_t(kSysCfg2Base | (band << 6) | (space << 4));
    regs_[SYSCONFIG3] = uint16_t(regs_[SYSCONFIG3] | kSysCfg3Seek);
    // De-emphasis (datasheet: 75 us USA, 50 us Europe/Australia/Japan). Region
    // order EU/US/AU/JP from the spec 05 config table. UNVERIFIED [I]: the stock
    // "different band register bits" of region 2 are not recorded.
    uint16_t sc1 = regs_[SYSCONFIG1] & uint16_t(~SC1_DE);
    if (region_ != 1) sc1 |= SC1_DE;
    regs_[SYSCONFIG1] = sc1;
}

uint16_t Si4703Radio::channelFor(float mhz) const {
    if (mhz < bandMin_) mhz = bandMin_;
    if (mhz > bandMax_) mhz = bandMax_;
    float c = (mhz - bottom_) / step_ + 0.5f;
    return c < 0 ? 0 : uint16_t(c) & 0x3FF;
}

float Si4703Radio::freqFor(uint16_t chan) const { return bottom_ + float(chan & 0x3FF) * step_; }

void Si4703Radio::setBand(float minMHz, float maxMHz, float stepMHz) {
    if (stepMHz <= 0.0f || maxMHz <= minMHz) return;
    bandMin_ = minMHz;
    bandMax_ = maxMHz;
    step_ = stepMHz;
    applyBandBits();
    if (state_ == kReady) {
        if (writeRegs()) {
            // Re-tune: CHAN is relative to the band bottom and spacing.
            pendingFreq_ = freq_;
            pendingTune_ = true;
        } else {
            fail();
        }
    }
}

bool Si4703Radio::tune(float mhz) {
    if (state_ == kOff) return false;
    pendingFreq_ = mhz;
    pendingTune_ = true;
    pendingSeek_ = 0;
    freq_ = freqFor(channelFor(mhz));   // shown right away; READCHAN confirms
    if (state_ == kReady) startTune();
    return true;
}

bool Si4703Radio::seek(bool up) {
    if (state_ == kOff) return false;
    pendingSeek_ = up ? 1 : -1;
    if (state_ == kReady) startSeek();
    return true;
}

bool Si4703Radio::busy() const {
    return state_ != kOff && (state_ != kReady || pendingTune_ || pendingSeek_ != 0);
}

void Si4703Radio::startTune() {
    pendingTune_ = false;
    regs_[POWERCFG] = uint16_t(regs_[POWERCFG] & ~PWR_SEEK);
    regs_[CHANNEL] = uint16_t(CH_TUNE | channelFor(pendingFreq_));
    if (!writeRegs()) return fail();
    state_ = kTuning;
    t0_ = millis();
}

void Si4703Radio::startSeek() {
    // Datasheet seek: SKMODE 0 = wrap at the band limit, stop after one lap.
    const bool up = pendingSeek_ > 0;
    pendingSeek_ = 0;
    regs_[CHANNEL] = uint16_t(regs_[CHANNEL] & ~CH_TUNE);
    uint16_t p = uint16_t(regs_[POWERCFG] & ~(PWR_SKMODE | PWR_SEEKUP));
    regs_[POWERCFG] = uint16_t(p | PWR_SEEK | (up ? PWR_SEEKUP : 0));
    if (!writeRegs()) return fail();
    state_ = kSeeking;
    t0_ = millis();
}

void Si4703Radio::service() {
    if (state_ == kOff) return;
    const uint32_t now = millis();
    switch (state_) {
        case kOsc:
            if (now - t0_ < kOscMs) return;
            // spec 01 §5.6: SYSCONFIG3 |= 0x48, POWERCFG = 0xE801, SYSCONFIG2 band.
            regs_[POWERCFG] = kPowerCfg;
            regs_[CHANNEL] = 0;
            applyBandBits();
            if (!writeRegs()) return fail();
            state_ = kPowerUp;
            t0_ = now;
            return;
        case kPowerUp:
            if (now - t0_ < kPowerUpMs) return;
            // spec 01 §5.6 then sets SYSCONFIG1 |= 0x9004 (RDS + GPIO2 interrupt);
            // OpenTracker polls STC and uses no RDS, so only DE (set above) is kept.
            state_ = kReady;
            if (!pendingTune_ && pendingSeek_ == 0) {
                pendingFreq_ = freq_;
                pendingTune_ = true;
            }
            break;
        case kTuning:
        case kSeeking: {
            if (!readRegs(2)) return fail();
            const bool stc = regs_[STATUSRSSI] & ST_STC;
            const uint32_t limit = state_ == kTuning ? kTuneTimeoutMs : kSeekTimeoutMs;
            if (!stc && now - t0_ < limit) return;
            // Done (or timed out): READCHAN holds the channel; clear TUNE/SEEK and
            // wait for STC to drop (datasheet tune/seek flow).
            // A failed seek (SF/BL set) also leaves READCHAN where it stopped.
            freq_ = freqFor(regs_[READCHAN]);
            regs_[CHANNEL] = uint16_t((regs_[CHANNEL] & ~CH_TUNE & ~0x3FFu) | (regs_[READCHAN] & 0x3FF));
            regs_[POWERCFG] = uint16_t(regs_[POWERCFG] & ~PWR_SEEK);
            if (!writeRegs()) return fail();
            state_ = state_ == kTuning ? kTuneEnd : kSeekEnd;
            t0_ = now;
            return;
        }
        case kTuneEnd:
        case kSeekEnd:
            if (!readRegs(2)) return fail();
            if ((regs_[STATUSRSSI] & ST_STC) && now - t0_ < kStcClearTimeoutMs) return;
            state_ = kReady;
            break;
        default:
            break;
    }
    if (state_ != kReady) return;
    if (pendingSeek_ != 0) return startSeek();
    if (pendingTune_) return startTune();
    if (now - lastStatus_ >= 500) {
        lastStatus_ = now;
        if (!readRegs(1)) return fail();
        rssi_ = regs_[STATUSRSSI] & 0xFF;
        stereo_ = regs_[STATUSRSSI] & ST_ST;
    }
}

}  // namespace hal
}  // namespace ot
