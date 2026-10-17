// SIDTracker -- MASTER page (Master key): the mixer while playing. Rows
// (Up/Down, jog wheel): master volume, SID balance, drum volume, effects
// on / off, delay / chorus / reverb mix. Right: level meters for SID1, SID2,
// the drums and the output (red = clipping) and the CPU load. F1 effects
// on / off; F8 (or Shift+Master) the benchmark page.
#pragma once

#include "ui/screen.h"

namespace sidt {
namespace ui {

class MasterScreen final : public Screen {
public:
    const char* title() const override { return "MASTER"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    enum Row { kVolume, kBalance, kDrums, kFx, kDelay, kChorus, kReverb, kRows };
    int sel_ = 0;
    float level_[4] = {};   // displayed meters (peak hold with decay)
    bool clip_ = false;
    uint32_t clipUntil_ = 0;
};

}  // namespace ui
}  // namespace sidt
