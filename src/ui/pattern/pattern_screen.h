// SIDTracker -- Pattern screen (PLAN.md §2 primary view, §7 pattern model).
//
// Six tracks, NOTE | INST | CMD per row, nothing else. Cursor fields per
// track: note, instrument (2 digits), command, parameter (2 digits).
//   Rec            edit mode (red cursor); edits need it
//   pads           note at the cursor (and preview); hex digits on hex fields
//   jog wheel      value under the cursor (Shift: octave / 0x10); not editing: rows
//   Up/Down        rows (Shift: 16), Left/Right fields (Shift: tracks)
//   Note/Instr/Fx1 jump to that field; Fx2 key-off "==="
//   Delete         clear the field (Shift: delete the row, pull the track up)
//   Insert         insert an empty row in the track
//   Copy           copy the track (Shift+Copy: paste)
//   F1/F2 pattern  F3/F4 octave  F5/F6 instrument  F7 step  F8 length
// Right of track 6 is the PCM drum column (M8): one kit slot per row; with
// Rec on the first 16 pads enter slots 1-16, the jog wheel steps through them.
#pragma once

#include "tracker/song/song.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

class PatternScreen final : public Screen {
public:
    static constexpr int kFields = 6;   // note, inst hi, inst lo, cmd, param hi, param lo
    static constexpr int kRows = 18;    // visible rows (display-list budget)

    const char* title() const override { return "PATTERN"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    bool midiNote(uint8_t noteValue) override;
    bool padColour(int p, uint32_t& rgb) override;
    bool hexPads() const;   // edit mode on an instrument / command field: pads 1-16 type hex
    void writeInstrument(uint8_t inst);   // the picker's Insert (edit mode)
    void encoder(int delta) override;
    bool animated() const override { return true; }

    int row() const { return row_; }
    void reset() { row_ = field_ = 0; }

private:
    Pattern& pat();
    Cell& cell();
    void moveRow(int d);
    void moveField(int d);
    void advance();
    void enterHex(int digit);
    void edited();
    void setDrum(uint8_t slot);

    int row_ = 0;

    int viewTop_ = 0;   // first row shown while following playback
    int field_ = 0;
    bool follow_ = true;
    Cell clip_[kMaxRows] = {};
    int clipRows_ = 0;
    uint8_t clipDrum_[kMaxRows] = {};
    int clipDrumRows_ = 0;
};

}  // namespace ui
}  // namespace sidt
