// SIDTracker -- SID ADSR envelope generator (envelope.h).
#include "sid/envelope.h"

namespace sidt {
namespace sid {

void Envelope::reset() { *this = Envelope{}; }

void Envelope::updatePeriod() {
    const uint8_t rate = state_ == kAttack ? attack_ : (state_ == kDecaySustain ? decay_ : release_);
    period_ = kRatePeriod[rate & 15];
}

void Envelope::writeAD(uint8_t ad) {
    attack_ = uint8_t(ad >> 4);
    decay_ = uint8_t(ad & 15);
    updatePeriod();
}

void Envelope::writeSR(uint8_t sr) {
    sustain_ = uint8_t(sr >> 4);
    release_ = uint8_t(sr & 15);
    updatePeriod();
}

void Envelope::setGate(bool on) {
    if (on == gate_) return;
    gate_ = on;
    // Gate on restarts the attack from the current level; gate off releases
    // from wherever the envelope is. The rate counter is not reset.
    state_ = on ? kAttack : kRelease;
    expCounter_ = 0;
    updatePeriod();
}

}  // namespace sid
}  // namespace sidt
