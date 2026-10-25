// SIDTracker -- File screen (PLAN.md §16).
//
// PROJECTS: native projects in /SIDTracker/Projects. F1 load, F2 save,
// F3 save as new, F4 new song, F6 export .sid (shows what plays on a C64,
// second press writes /SIDTracker/SID/NAME.sid), F8 delete.
// GOATTRACKER: .sng / .ins in /SIDTracker/GoatTracker. F1 import (a song
// replaces the current one, an instrument goes into the current
// instrument slot), F2 export the song (shows the compatibility check,
// second press writes), F3 export the current instrument, F8 delete.
// F5 switches between the two. Destructive actions ask for a second press.
#pragma once

#include "compatibility/goat/import.h"
#include "compatibility/sid/sid_export.h"
#include "tracker/song/song.h"
#include "ui/screen.h"

namespace sidt {
namespace ui {

class FileScreen final : public Screen {
public:
    const char* title() const override { return goat_ ? "FILE GT" : "FILE"; }
    void enter() override;
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;

private:
    static constexpr int kMax = 64;
    static constexpr int kNameCap = kNameLen + 5;   // name + extension
    void refresh();
    bool confirm(uint8_t action, const char* question, const char* ok);
    bool projectButton(uint8_t id);
    bool goatButton(uint8_t id);

    char names_[kMax][kNameCap] = {};
    char saveName_[kNameLen + 1] = {};   // SAVE NEW: the typed name (kept for the overwrite question)
    int count_ = 0, sel_ = 0;
    bool goat_ = false;
    bool reportShown_ = false;
    goat::Report exportCheck_;
    bool sidFailed_ = false;
};

}  // namespace ui
}  // namespace sidt
