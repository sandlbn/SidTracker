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
// Play: start / stop (Shift+Play loops the current pattern). Rec: edit mode.
#pragma once

#include <cstdint>

#include "hal_api.h"
#include "tracker/song/song.h"
#include "ui/buttons.h"
#include "ui/gfx.h"
#include "ui/extended/extended_screen.h"
#include "ui/master/master_screen.h"
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
                                kCount };

class Ui {
public:
    void init(App& app);
    void handle(const ot::InputEvent& ev);
    void update(uint32_t nowMs);
    void show(ScreenId id);
    ScreenId current() const { return current_; }
    void message(const char* text, uint32_t ms = 1500);
    void songChanged();                // after load / new: reset cursors
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
    // Plays the held pads' notes again with `inst` (sound design: hear a
    // change at once).
    void retriggerHeld(uint8_t inst);
    uint32_t now() const { return now_; }
    uint32_t frames() const { return frames_; }
    uint32_t cpuPermille(bool peak) const { return bench_.cpuPermille(peak); }

private:
    Screen* screen(ScreenId id);
    void drawChrome(ot::Gfx& g, Screen& s);
    void drawHelp(ot::Gfx& g);
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
    bool dirty_ = true;
    bool help_ = false;
    uint32_t now_ = 0, lastFrame_ = 0, lastLeds_ = 0;
    uint32_t frames_ = 0;
    char msg_[64] = {};
    uint32_t msgUntil_ = 0;
    uint8_t padHeld_[ot::kPadCount] = {};  // note value held per pad (audition)
    int padChannel_[ot::kPadCount] = {};
    uint64_t padsDown_ = 0;
};

}  // namespace ui
}  // namespace sidt
