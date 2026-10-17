// SIDTracker -- Song screen (PLAN.md §14): the order list.
//
//   POS   PATTERN   TRANSPOSE   REPEAT
// Up/Down position, Left/Right column, jog wheel changes (Shift: x16 /
// octave), Insert duplicates the entry below, Delete removes it, F7 EDIT PAT
// opens the entry's pattern on the Pattern screen. Play starts the song at
// the cursor. F1 restart here, F2/F3 BPM, F4/F5 speed, F6 Authentic /
// Extended.
#pragma once

#include "ui/screen.h"

namespace sidt {
namespace ui {

class SongScreen final : public Screen {
public:
    const char* title() const override { return "SONG"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    int col_ = 0;   // 0 pattern, 1 transpose, 2 repeat
};

}  // namespace ui
}  // namespace sidt
