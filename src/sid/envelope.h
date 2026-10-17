// SIDTracker -- SID ADSR envelope generator in SID-clock units.
//
// A 15-bit rate counter counts SID clocks up to the period of the current
// rate; each hit steps the 8-bit level (attack: +1 every hit; decay and
// release: -1 every expPeriod(level) hits). Lowering the period below the
// running counter makes the counter wrap through 0x7FFF first: the chip's
// well-known ADSR delay, which hard-restart techniques rely on.
#pragma once

#include <cstdint>

#include "sid/tables.h"

namespace sidt {
namespace sid {

class Envelope {
public:
    enum State : uint8_t { kAttack, kDecaySustain, kRelease };

    void reset();
    void writeAD(uint8_t ad);
    void writeSR(uint8_t sr);
    void setGate(bool on);

    // Advance by `clocks` SID clocks (bounded: <= 3 steps per 23 clocks).
    void clock(uint32_t clocks) {
        // Released to zero or holding the sustain level: rate hits change
        // nothing but the counter phase, which stays below the period.
        if (counter_ < period_ && ((state_ == kRelease && level_ == 0) ||
                                   (state_ == kDecaySustain && level_ == uint8_t(sustain_ * 0x11)))) {
            counter_ = uint16_t((counter_ + clocks) % period_);
            return;
        }
        // Attack well below the top: every hit is +1, so count them at once.
        if (state_ == kAttack && counter_ < period_) {
            const uint32_t sum = counter_ + clocks;
            const uint32_t hits = sum / period_;
            if (level_ + hits < 0xFF) {
                level_ = uint8_t(level_ + hits);
                counter_ = uint16_t(sum % period_);
                return;
            }
        }
        while (clocks) {
            const uint32_t toHit = counter_ < period_ ? uint32_t(period_ - counter_)
                                                      : uint32_t(0x8000u - counter_ + period_);
            if (toHit > clocks) {
                counter_ = uint16_t((counter_ + clocks) & 0x7FFF);
                return;
            }
            clocks -= toHit;
            counter_ = 0;
            step();
        }
    }

    uint8_t level() const { return level_; }
    State state() const { return state_; }
    bool gate() const { return gate_; }
    uint16_t counter() const { return counter_; }

private:
    void step() {
        switch (state_) {
            case kAttack:
                level_ = uint8_t(level_ + 1);
                if (level_ == 0xFF) {
                    state_ = kDecaySustain;
                    expCounter_ = 0;
                    updatePeriod();
                }
                break;
            case kDecaySustain:
                if (++expCounter_ < expPeriod(level_)) break;
                expCounter_ = 0;
                if (level_ > uint8_t(sustain_ * 0x11)) --level_;
                break;
            case kRelease:
                if (++expCounter_ < expPeriod(level_)) break;
                expCounter_ = 0;
                if (level_) --level_;   // the counter freezes at zero
                break;
        }
    }
    void updatePeriod();

    uint16_t counter_ = 0;
    uint16_t period_ = 9;
    uint8_t expCounter_ = 0;
    uint8_t level_ = 0;
    uint8_t attack_ = 0, decay_ = 0, sustain_ = 0, release_ = 0;
    State state_ = kRelease;
    bool gate_ = false;
};

}  // namespace sid
}  // namespace sidt
