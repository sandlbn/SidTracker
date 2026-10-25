// SIDTracker -- one emulated SID chip (sid.h).
#include "sid/sid.h"

namespace sidt {
namespace sid {

void Chip::init(Model model, uint32_t clockHz, uint32_t sampleRateMilliHz) {
    model_ = model;
    clockHz_ = clockHz;
    sampleRateMilliHz_ = sampleRateMilliHz ? sampleRateMilliHz : 44100000;
    updateClockStep();
    filter_.setModel(model);
    reset();
}

void Chip::updateClockStep() {
    clocksPerSample16_ = uint32_t(uint64_t(clockHz_) * 65536u * 1000u / sampleRateMilliHz_);
    clocksPerSample24_ = uint32_t(uint64_t(clockHz_) * 16777216u * 1000u / sampleRateMilliHz_);
}

void Chip::reset() {
    for (Voice& v : voices_) {
        v.osc.reset();
        v.env.reset();
    }
    filter_.reset();
    for (uint8_t& r : regs_) r = 0;
    for (uint16_t& w : lastWave_) w = 0;
    volume_ = 0.0f;
    clockFrac_ = 0;
}

void Chip::setModel(Model m) {
    if (m == model_) return;
    model_ = m;
    filter_.setModel(m);
}

void Chip::setClock(uint32_t clockHz) {
    if (!clockHz || clockHz == clockHz_) return;
    clockHz_ = clockHz;
    updateClockStep();
    for (Voice& v : voices_) v.osc.setFreq(v.osc.freq(), clocksPerSample24_);
}

void Chip::write(uint8_t reg, uint8_t val) {
    if (reg >= kRegCount) return;
    regs_[reg] = val;
    if (reg < kVoices * kVoiceStride) {
        Voice& v = voices_[reg / kVoiceStride];
        const uint8_t base = uint8_t(reg - reg % kVoiceStride);
        switch (reg % kVoiceStride) {
            case kFreqLo:
            case kFreqHi:
                v.osc.setFreq(uint16_t(regs_[base + kFreqLo] | (regs_[base + kFreqHi] << 8)), clocksPerSample24_);
                break;
            case kPwLo:
            case kPwHi:
                v.osc.setPulseWidth(uint16_t(regs_[base + kPwLo] | ((regs_[base + kPwHi] & 0x0F) << 8)));
                break;
            case kControl:
                v.osc.setControl(val);
                v.env.setGate((val & kGate) != 0);
                break;
            case kAD: v.env.writeAD(val); break;
            case kSR: v.env.writeSR(val); break;
        }
        return;
    }
    switch (reg) {
        case kFcLo:
        case kFcHi:
            filter_.setCutoff(uint16_t((regs_[kFcHi] << 3) | (regs_[kFcLo] & 7)));
            break;
        case kResFilt: filter_.setResonance(uint8_t(val >> 4)); break;
        case kModeVol:
            filter_.setMode(val);
            volume_ = float(val & 15) * (1.0f / 15.0f);
            break;
        default: break;
    }
}

uint8_t Chip::read(uint8_t reg) const {
    switch (reg) {
        case kOsc3: return uint8_t(lastWave_[2] >> 4);
        case kEnv3: return voices_[2].env.level();
        default: return 0;   // write-only registers
    }
}

void Chip::renderVoices(int n) {
    if (n > kMaxRender) n = kMaxRender;
    const uint8_t route = regs_[kResFilt];
    const bool v3Off = (regs_[kModeVol] & k3Off) != 0;
    constexpr float kEnvScale = 1.0f / 255.0f;
    for (int i = 0; i < n; ++i) {
        clockFrac_ += clocksPerSample16_;
        const uint32_t clocks = clockFrac_ >> 16;
        clockFrac_ &= 0xFFFFu;
        for (Voice& v : voices_) v.env.clock(clocks);
        for (Voice& v : voices_) v.osc.advance();
        for (int v = 0; v < kVoices; ++v) voices_[v].osc.applySync(voices_[(v + 2) % kVoices].osc);
        float f = 0.0f, d = 0.0f, pl = 0.0f, pr = 0.0f;
        for (int v = 0; v < kVoices; ++v) {
            Voice& vc = voices_[v];
            uint16_t w12 = 0;
            const float w = vc.osc.output(voices_[(v + 2) % kVoices].osc, model_, w12);
            lastWave_[v] = w12;
            if (muted_[v]) continue;
            const float out = w * float(vc.env.level()) * (kEnvScale * gain_[v]);
            if (route & (1u << v)) f += out;
            else if (v == 2 && v3Off) continue;
            else if (panOn_[v]) {
                pl += out * panGain_[v][0];
                pr += out * panGain_[v][1];
            } else {
                d += out;
            }
        }
        filtIn_[i] = f;
        direct_[i] = d;
        panL_[i] = pl;
        panR_[i] = pr;
    }
}

void Chip::renderFilter(int n, float* out) {
    if (n > kMaxRender) n = kMaxRender;
    const float vol = volume_;
    for (int i = 0; i < n; ++i) out[i] = (direct_[i] + filter_.process(filtIn_[i])) * vol;
    if (anyPan_)
        for (int i = 0; i < n; ++i) {
            panL_[i] *= vol;
            panR_[i] *= vol;
        }
}

}  // namespace sid
}  // namespace sidt
