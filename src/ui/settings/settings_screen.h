// SIDTracker -- Settings screen (PLAN.md §2: SID model, clock, audio and
// compatibility settings) plus the firmware update picker (way back to
// stock, APPROACH.md §1). Up/Down select, jog wheel changes; text rows:
// Left/Right move the character cursor. F7 jumps to the FIRMWARE UPDATE
// row; there the jog wheel picks the image and F8 INSTALL (or Insert),
// pressed twice, flashes it.
#pragma once

#include "tracker/song/song.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

class SettingsScreen final : public Screen {
public:
    const char* title() const override { return "SETTINGS"; }
    void enter() override;
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;

private:
    enum Row { kSid1, kSid2, kClock, kBalance, kVolume, kTheme, kMode, kHrAD, kHrSR, kName, kAuthor, kFirmware,
               kRowCount };
    void scanImages();

    int sel_ = 0;
    int cursor_ = 0;
    static constexpr int kMaxImages = 8;
    char images_[kMaxImages][64] = {};
    int imageCount_ = 0, image_ = 0;
    bool confirm_ = false;
};

// Jog-wheel character edit of a fixed-size name at `pos` (keeps it
// terminated, pads with spaces before `pos`).
void editNameChar(char* name, int pos, int delta);

}  // namespace ui
}  // namespace sidt
