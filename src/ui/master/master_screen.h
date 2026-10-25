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
    // Page 0: the mixer (F1-F6 tracks, F7 drums, F8 master: hold + jog =
    // level, tap = mute, Shift + tap = solo, Delete = default). Page 1 (the
    // Master key again): balance, effects, meters per chip, CPU.
    enum Row { kVolume, kBalance, kDrums, kFx, kDelay, kChorus, kReverb, kRows };
    enum Strip { kTrack1 = 0, kDrumStrip = 6, kMasterStrip = 7, kStrips = 8 };
    void drawMixer(ot::Gfx& g);
    void drawSettings(ot::Gfx& g);
    void changeLevel(int strip, int delta);
    int page_ = 0;
    int sel_ = 0;          // page 1 row
    int strip_ = 0;        // page 0 selected strip
    int heldF_ = -1;       // F-key held on page 0
    bool jogged_ = false;  // the held F-key turned a level (no mute on release)
    float stripLevel_[kStrips] = {};
    float level_[4] = {};   // displayed meters (peak hold with decay)
    bool clip_ = false;
    uint32_t clipUntil_ = 0;
};

}  // namespace ui
}  // namespace sidt
