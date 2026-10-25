// SIDTracker -- SID PLAYER screen (File > F7): browse the card for .sid
// files and play them on the dual-SID engine (app SID player, src/c64).
//   Up/Down / jog wheel   select (Shift: 16)
//   Insert / F1 PLAY      open the folder / play the file
//   Play                  play the selected file, or stop
//   F2 STOP   F3 / F4 or Left / Right  previous / next subtune   F5 UP folder
// Leaving the screen stops the tune.
#pragma once

#include "ui/screen.h"

namespace sidt {
namespace ui {

class SidPlayerScreen final : public Screen {
public:
    const char* title() const override { return "SID PLAYER"; }
    void enter() override;
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;
    bool animated() const override { return true; }

private:
    static constexpr int kRows = 18;
    static constexpr int kName = 56;
    // The folder is listed once into a cache in the scratch memory (above
    // what sidPlay() uses), so paging never touches the card.
    static constexpr uint32_t kCacheOffset = 256u * 1024u;
    static constexpr int kRecord = 64;   // [0] = folder flag, [1..] = name
    static constexpr int kMaxEntries = int((512u * 1024u - kCacheOffset) / kRecord);
    void refresh();
    char* entry(int i);
    void fill();
    void move(int d);
    void activate();
    void up();
    void playSelected(int song);
    void subtune(int d);

    char path_[160] = "SIDTracker/SID";
    char names_[kRows][kName] = {};   // the visible window
    bool dirs_[kRows] = {};
    int count_ = 0;          // entries including ".." when not at the root
    int listed_ = 0;         // entries in the cache
    int sel_ = 0, top_ = 0;
    float level_[3] = {};
};

}  // namespace ui
}  // namespace sidt
