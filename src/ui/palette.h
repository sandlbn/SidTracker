// SIDTracker -- colour themes. The col:: colours (screen.h) are the active
// palette; applyTheme() switches it at runtime (Config > THEME, saved in
// /SIDTracker/settings.txt).
#pragma once

#include <cstdint>

namespace sidt {
namespace ui {

struct Palette {
    const char* name;
    uint32_t bg, panel, panelHi, rowHi, text, dim, faint, cursor, edit, playRow, note, inst, cmd, sid1, sid2, warn,
        bad, good, onCursor;
};

int themeCount();
const Palette& theme(int i);
void applyTheme(int i);   // out of range -> 0
int currentTheme();

}  // namespace ui
}  // namespace sidt
