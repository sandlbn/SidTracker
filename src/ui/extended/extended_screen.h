// SIDTracker -- Extended screen (M8; Sample Loader key): features beyond
// GoatTracker, all optional and listed by the GoatTracker export check.
//   F1 EFFECTS      delay (tempo synced), chorus, reverb; mix 0 = off
//   F2 DRUM KIT     drum volume, the 16 kit slots; pads / Insert audition,
//                   F5 reloads /SIDTracker/Kit/NNname.wav
//   F3 MODULATION   two LFOs (pulse width / cutoff), per-voice pan
//   F8              all effects on / off (global, PLAN.md §13)
// Up/Down select, jog wheel changes (Shift: x10).
#pragma once

#include "ui/screen.h"

namespace sidt {
namespace ui {

class ExtendedScreen final : public Screen {
public:
    enum Page : uint8_t { kEffects, kKit, kModulation, kPages };
    const char* title() const override { return "EXTENDED"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    void encoder(int delta) override;
    bool animated() const override { return page_ == kModulation; }

private:
    int rows() const;
    void drawEffects(ot::Gfx& g);
    void drawKit(ot::Gfx& g);
    void drawModulation(ot::Gfx& g);
    int modRows() const { return 2 * 4 + 6; }   // 2 LFOs x 4 fields, 6 pans
    void modEdit(int d);

    Page page_ = kEffects;
    int sel_ = 0;
};

}  // namespace ui
}  // namespace sidt
