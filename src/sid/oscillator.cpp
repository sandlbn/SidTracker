// SIDTracker -- SID oscillator (oscillator.h).
#include "sid/oscillator.h"

#include "sid/sid.h"

namespace sidt {
namespace sid {


void Oscillator::reset() { *this = Oscillator{}; }

void Oscillator::setFreq(uint16_t freq, uint32_t clocksPerSample24) {
    freq_ = freq;
    step_ = uint32_t((uint64_t(freq) * clocksPerSample24) >> 16);
}

void Oscillator::setPulseWidth(uint16_t pw12) { pw_ = uint16_t(pw12 & 0xFFF); }

void Oscillator::setControl(uint8_t ctrl) {
    if (ctrl & kTest) {
        // Test bit: accumulator held at zero, noise shift register refilled.
        phase_ = 0;
        lfsr_ = 0x7FFFFF;
    }
    ctrl_ = ctrl;
}

}  // namespace sid
}  // namespace sidt
