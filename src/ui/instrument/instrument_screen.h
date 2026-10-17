// SIDTracker -- Instrument screen (PLAN.md §9): GENERAL DESIGN WAVE PULSE
// FILTER TABLES SOUNDS on F1..F7, F8 next instrument (Shift+F8 previous).
//
// DESIGN: the sound as synth controls (waveform, attack transient, ADSR,
// pulse width and PWM, filter type / cutoff / resonance / sweep, vibrato,
// hard restart). Up/Down rows, Left/Right columns, jog wheel changes
// (Shift: coarse). Every change rewrites the instrument's programs
// (tracker/song/designer.h) and replays the held pads.
//
// SOUNDS: the sound library (category, preset; Insert loads it into the
// instrument, a second push when the slot is not empty) and the arpeggiator
// insert (chord, order, ticks per note, waveform; Insert writes the wave
// program). A preset's filter routes the voice of the cursor track.
//
// Parameter pages: Up/Down select, jog wheel changes (Shift: x16).
// Table pages: Up/Down rows, Left/Right columns, jog wheel edits the byte,
// with Rec on the first 16 pads type hex digits; Shift+jog moves the
// instrument's pointer, Insert points it at the cursor row, Delete clears
// the row. Pads otherwise audition the instrument on the cursor channel.
#pragma once

#include "tracker/song/song.h"
#include "tracker/song/designer.h"
#include "tracker/song/sound_library.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

// Decoded meaning of a table row for the help line.
void describeTableRow(TableKind kind, uint8_t left, uint8_t right, char* out, int cap);

class TableView {
public:
    TableKind kind = kWaveTable;
    int row = 1;           // 1..kTableLen
    int col = 0;           // 0 = left byte, 1 = right byte
    int nibble = 0;        // hex entry position

    void draw(ot::Gfx& g, const Song& s, int x, int y, int rows, bool focused, uint8_t pointer);
    void move(int d) { row = ((row - 1 + d) % kTableLen + kTableLen) % kTableLen + 1; nibble = 0; }
    void edit(Song& s, int delta);
    void hex(Song& s, int digit);
    void clearRow(Song& s);
};

class InstrumentScreen final : public Screen {
public:
    enum Page : uint8_t { kGeneral, kDesign, kWave, kPulse, kFilter, kTables, kSounds, kPages };

    const char* title() const override { return "INSTRUMENT"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    bool pad(int p, bool down, uint8_t vel) override;
    void encoder(int delta) override;

private:
    Instrument& inst();
    int paramCount() const;
    void editParam(int delta);
    void drawParams(ot::Gfx& g);
    void drawEnvelope(ot::Gfx& g, int x, int y, int w, int h);
    TableView& view();
    uint8_t* pointerFor(TableKind k);
    void edited();
    void drawSounds(ot::Gfx& g);
    void drawDesign(ot::Gfx& g);
    void designEncoder(int d);
    bool designButton(uint8_t id);
    designer::Design& design();
    bool soundsButton(uint8_t id);
    void soundsEncoder(int d);
    int presetIndex() const { return library::categoryFirst(category_) + preset_; }

    Page page_ = kGeneral;
    int sel_ = 0;
    int nameCursor_ = 0;
    TableView views_[kTableKinds];   // one per table kind
    int tableFocus_ = 0;             // TABLES page: focused table
    // SOUNDS page.
    int sndRow_ = 0;                 // 0 category, 1 preset, 2..5 arp fields
    int category_ = 0, preset_ = 0;
    library::Arp arp_;
    bool loadPending_ = false;       // second push overwrites a used instrument
    // DESIGN page.
    designer::Design design_;
    int designSlot_ = -1;            // instrument design_ was read from
    int dRow_ = 0;
};

}  // namespace ui
}  // namespace sidt
