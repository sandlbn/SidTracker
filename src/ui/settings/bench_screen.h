// SIDTracker -- benchmark page (PLAN.md §6, M0 acceptance): audio buffer
// period, cycles available / consumed / peak, CPU utilisation, underruns,
// per-section cycles. F1 stress load (all voices,
// both filters), F2 reset peaks.
#pragma once

#include "app/engine.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

class BenchScreen final : public Screen {
public:
    const char* title() const override { return "BENCHMARK"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool animated() const override { return true; }
    // Polled by the Ui every pass (also while another screen is shown, so
    // the peaks cover everything since the last reset).
    void poll(uint32_t nowMs);
    // Audio CPU load in permille of the block budget: the last second's
    // average, or the peak since the last reset.
    uint32_t cpuPermille(bool peak) const;

private:
    Profile window_;     // last one-second window
    uint32_t peak_[Profile::kSections] = {};
    uint32_t lastPoll_ = 0;
    uint32_t drvMax_ = 0, drvAvg_ = 0;
    uint32_t underruns_ = 0;
    bool haveDriver_ = false;
};

}  // namespace ui
}  // namespace sidt
