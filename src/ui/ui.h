// SIDTracker -- user interface: screens over IDisplay + ot::Gfx (PLAN.md §2,
// §9-§10, §14). Main loop only; never touches audio state except through
// the Engine API and plain song edits.
//
// Module keys (front panel labels in brackets):
//   Pattern            -> Pattern       [Instr. Params] -> Instrument
//   [Performance]      -> Perform       [Song]          -> Song
//   [Config]           -> Settings      [File]          -> File
//   [Master]           -> Benchmark     [Help]          -> key help overlay
//   [Sample Loader]    -> Extended (effects, drum kit, modulation)
// Play: start / stop (Shift+Play loops the current pattern). Rec: edit mode;
// Shift+Rec undo, Shift+Sample Rec redo.
#pragma once

#include <cstdint>

#include "hal_api.h"
#include "tracker/song/song.h"
#include "ui/buttons.h"
#include "ui/gfx.h"
#include "ui/extended/extended_screen.h"
#include "ui/master/master_screen.h"
#include "ui/sidplayer/sid_player_screen.h"
#include "ui/instrument/instrument_screen.h"
#include "ui/pattern/pattern_screen.h"
#include "ui/perform/perform_screen.h"
#include "ui/screen.h"
#include "ui/settings/bench_screen.h"
#include "ui/settings/file_screen.h"
#include "ui/settings/settings_screen.h"
#include "ui/song/song_screen.h"

namespace sidt {
class App;
namespace ui {

enum class ScreenId : uint8_t { kPattern, kInstrument, kPerform, kSong, kSettings, kFile, kBench, kExtended, kMaster,
                                kSidPlayer, kCount };

class Ui {
public:
    void init(App& app);
    void handle(const ot::InputEvent& ev);
    void update(uint32_t nowMs);
    void show(ScreenId id);
    ScreenId current() const { return current_; }
    // Results of commands (saved, exported, errors, questions): a framed
    // box above the bottom bar.
    void message(const char* text, uint32_t ms = 1500);
    // Routine feedback (copied, follow on, points here): a short line in the
    // title bar, no box (UX plan P0.5).
    void status(const char* text, uint32_t ms = 1200);
    // Confirm dialog (UX plan P1.7): a screen asks before a destructive
    // action. The first call opens the dialog and returns false; F8 (`ok`)
    // re-sends button `action` to the screen, and then confirm() returns
    // true; F7 or any other key cancels. Every other input waits.
    bool confirm(Screen* s, uint8_t action, const char* question, const char* ok = "OK");
    bool dialogOpen() const { return ask_.open; }
    // True while button `action` is being re-sent by a confirmed dialog.
    bool confirming(uint8_t action) const { return confirmed_ == action; }
    void undoRedo(bool redo);          // Shift+Rec / Shift+Sample Rec
    void songChanged();                // after load / new: reset cursors
    void songEdited();                 // after undo / redo: drop caches, clamp cursors
    // Instrument picker (UX plan P1.1): hold Instr anywhere (or tap it on the
    // Instrument screen). A 12 x 4 grid like the pads; pads pick and play a
    // slot, jog / arrows move, F8 next page; releasing Instr (or Instr / F8
    // OK when opened by a tap) chooses it. Insert writes it into the pattern
    // cell in edit mode.
    bool pickerOpen() const { return pick_.open; }
    // Pad keyboard (UX plan P2.4): types a name on the 48 pads. F8 OK
    // re-sends button `action` to the screen, which then reads the text
    // from keyboardResult(action); F7 / any module key cancels. `fileName`
    // allows only A-Z 0-9 _ - (no spaces).
    void keyboard(Screen* s, uint8_t action, const char* title, const char* initial, bool fileName = false);
    const char* keyboardResult(uint8_t action) const {
        return kb_.done && kb_.action == action ? kb_.text : nullptr;
    }
    bool keyboardOpen() const { return kb_.open; }
    void requestRedraw() { dirty_ = true; }

    App& app() { return *app_; }
    Song& song();
    // Shared editing state.
    bool shift = false;
    bool edit = false;                 // Rec: pattern edit mode
    uint8_t octave = 3;                // pad bottom row octave
    uint8_t inst = 1;                  // current instrument
    uint8_t step = 1;                  // rows advanced after an entry
    uint8_t pattern = 0;               // pattern being edited
    uint8_t orderPos = 0;              // song position (Song screen cursor)
    int channel = 0;                   // cursor channel (audition target)

    // Pad index -> note value (1..96) for the current octave; 0 = none.
    uint8_t padNote(int pad) const;
    // Preview notes on the cursor channel (pads held).
    void auditionOn(int pad, uint8_t inst);
    void auditionOff(int pad);
    // Plays `inst` at `note` while the pad is held (instrument pads).
    void auditionNote(int pad, uint8_t inst, uint8_t note);
    // Plays the held pads' notes again with `inst` (sound design: hear a
    // change at once).
    void retriggerHeld(uint8_t inst);
    // Plays `inst` for 300 ms on the cursor track when no pad is held (a
    // preset or arpeggio just loaded: hear it without touching a pad).
    void preview(uint8_t inst);
    // MIDI keyboard: MIDI note number (60 = C-4), on / off. Auditions on the
    // cursor track and lets the screen enter it (Pattern in edit mode).
    void midiNote(uint8_t midiNote, bool on);
    uint32_t now() const { return now_; }
    uint32_t frames() const { return frames_; }
    uint32_t cpuPermille(bool peak) const { return bench_.cpuPermille(peak); }

private:
    Screen* screen(ScreenId id);
    void drawChrome(ot::Gfx& g, Screen& s);
    void drawHelp(ot::Gfx& g);
    void drawPicker(ot::Gfx& g);
    bool pickerInput(const ot::InputEvent& ev);
    void openPicker(bool sticky);
    struct Kb {
        bool open = false, done = false, fileName = false;
        char title[32] = {};
        char text[kNameLen + 1] = {};
        Screen* target = nullptr;
        uint8_t action = 0;
    } kb_;
    void drawKeyboard(ot::Gfx& g);
    void keyboardInput(const ot::InputEvent& ev);
    struct Pick {
        bool open = false;
        bool sticky = false;      // opened by a tap: stays until Instr / F8 / F7
        uint8_t slot = 1;
        uint32_t instrDownAt = 0; // Instr held since (0 = not held)
        bool instrHeld = false;
    } pick_;
    void updateLeds();

    App* app_ = nullptr;
    ScreenId current_ = ScreenId::kPattern;
    PatternScreen pattern_;
    InstrumentScreen instrument_;
    PerformScreen perform_;
    SongScreen songScreen_;
    SettingsScreen settings_;
    FileScreen file_;
    BenchScreen bench_;
    ExtendedScreen extended_;
    MasterScreen master_;
    SidPlayerScreen sidPlayer_;
    bool dirty_ = true;
    bool help_ = false;
    uint32_t now_ = 0, lastFrame_ = 0, lastLeds_ = 0;
    uint32_t frames_ = 0;
    char msg_[64] = {};
    uint32_t msgUntil_ = 0;
    struct Ask {
        bool open = false;
        char text[72] = {};
        const char* ok = "OK";
        Screen* target = nullptr;
        uint8_t action = 0;
    } ask_;
    int confirmed_ = -1;   // action being re-sent after F8
    void drawDialog(ot::Gfx& g);
    uint32_t previewUntil_ = 0;
    uint64_t pickerPads_ = 0;   // pads pressed inside the picker (their release is the Ui's)
    int previewChannel_ = -1;
    char status_[48] = {};
    uint32_t statusUntil_ = 0;
    uint8_t padHeld_[ot::kPadCount] = {};  // note value held per pad (audition)
    int padChannel_[ot::kPadCount] = {};
    uint64_t padsDown_ = 0;
    int8_t midiHeld_[128] = {};   // track + 1 a held MIDI note sounds on, 0 = none
};

}  // namespace ui
}  // namespace sidt
