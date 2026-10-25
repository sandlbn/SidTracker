// SIDTracker -- Perform screen (PLAN.md §10): the six voices and both SID
// engines made explicit, live controls on top of the song.
//
// Rows: voices 1-6 (MUTE, SOLO, WAVE, PW), SID1 / SID2 filter (CUT, RES),
// balance. Up/Down row, Left/Right control, jog wheel changes, Insert
// toggles / resets. Pads: top row 1-6 mute, second row 1-6 solo, third row
// 1-6 cycle the waveform override; bottom row auditions notes.
// F1 clears every override.
//
// Automation (M7): with Rec on while the song plays, WAVE, PW, CUT and RES
// changes are recorded into the playing pattern's rows; a control keeps
// recording on every new row while it is being turned. Recorded values
// play back on their rows (marked * here). F2 clears the selected row's
// lanes in the current pattern (second press confirms).
#pragma once

#include "tracker/song/song.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

class PerformScreen final : public Screen {
public:
    const char* title() const override { return "PERFORM"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    void encoder(int delta) override;
    bool padColour(int p, uint32_t& rgb) override;
    bool animated() const override { return true; }

private:
    static constexpr int kRowsCount = 9;   // 6 voices, 2 filters, balance
    int controls(int row) const { return row < 6 ? 4 : (row < 8 ? 2 : 1); }
    void change(int row, int ctl, int delta, bool toggle);
    static int laneOf(int row, int ctl);   // automation lane or -1
    bool recording();
    int currentPattern();
    void record(int lane);
    void drawVoice(ot::Gfx& g, int ch, int x, int y);

    int row_ = 0, ctl_ = 0;
    int clearPattern_ = 0;   // CLR AUTO: the pattern playing when it was asked
    uint32_t touched_[kAutoLanes] = {};    // last change (ms) per lane, 0 = idle
    int lastPos_[kAutoLanes] = {};         // pattern * 256 + row last recorded
};

}  // namespace ui
}  // namespace sidt
