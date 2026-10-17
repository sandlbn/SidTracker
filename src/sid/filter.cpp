// SIDTracker -- SID filter (filter.h).
#include "sid/filter.h"

#include "sid/sid.h"

namespace sidt {
namespace sid {

void Filter::reset() {
    const Model m = model_;
    *this = Filter{};
    model_ = m;
    saturate_ = m == Model::k6581;
    update();
}

void Filter::setModel(Model m) {
    model_ = m;
    saturate_ = m == Model::k6581;
    update();
}

void Filter::setCutoff(uint16_t fc11) {
    fc_ = uint16_t(fc11 & 0x7FF);
    update();
}

void Filter::setResonance(uint8_t res4) {
    res_ = uint8_t(res4 & 15);
    update();
}

void Filter::setMode(uint8_t modeBits) {
    mode_ = uint8_t(modeBits & (kLP | kBP | kHP));
    lpOn_ = (mode_ & kLP) ? 1.0f : 0.0f;
    bpOn_ = (mode_ & kBP) ? 1.0f : 0.0f;
    hpOn_ = (mode_ & kHP) ? 1.0f : 0.0f;
}

void Filter::update() {
    g_ = tablesReady() ? cutoffG(model_)[fc_] : 0.1f;
    k_ = resonanceK(model_, res_);
    h_ = 1.0f / (1.0f + k_ * g_ + g_ * g_);
}

}  // namespace sid
}  // namespace sidt
