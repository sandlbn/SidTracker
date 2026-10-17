// SIDTracker -- colour themes (palette.h).
#include "ui/palette.h"

#include "ui/screen.h"

namespace sidt {
namespace ui {

namespace col {
uint32_t kBg, kPanel, kPanelHi, kRowHi, kText, kDim, kFaint, kCursor, kEdit, kPlayRow, kNote, kInst, kCmd, kSid1, kSid2,
    kWarn, kBad, kGood, kOnCursor;
}  // namespace col

namespace {

// clang-format off
const Palette kThemes[] = {
    // name          bg        panel     panelHi   rowHi     text      dim       faint     cursor    edit
    //               playRow   note      inst      cmd       sid1      sid2      warn      bad       good      onCursor
    {"SIDTRACKER",   0x0E1116, 0x1A2028, 0x283240, 0x1E2A38, 0xE6E8EA, 0x5E6874, 0x353D48, 0x2F7FD8, 0xC83C3C,
                     0x2E4A2E, 0xF2F2F2, 0x7FCBFF, 0xFFB04A, 0x5FD38D, 0xC08DFF, 0xFFD040, 0xFF5050, 0x50E070, 0xFFFFFF},
    // The C64's own colours (blue screen, light blue text and border), the
    // rest of its 16-colour palette for the accents.
    {"COMMODORE 64", 0x40318D, 0x352879, 0x5A4BB0, 0x4A3CA0, 0xA99CF0, 0x7869C4, 0x5546A8, 0xFFFFFF, 0x9F4E44,
                     0x2E6A6E, 0xFFFFFF, 0x70D0D8, 0xEDF171, 0x9AE29B, 0xC9887E, 0xEDF171, 0xCB7E75, 0x9AE29B, 0x40318D},
    // Green phosphor monitor.
    {"GREEN SCREEN", 0x020A04, 0x0A1E0E, 0x123A1A, 0x0C2812, 0x5CFF7A, 0x2A9A44, 0x16502A, 0x9CFFB0, 0xFFFFFF,
                     0x165A26, 0xA8FFB8, 0x5CFF7A, 0xC8FFD0, 0x5CFF7A, 0x9CFFB0, 0xE0FFE8, 0xFFFFFF, 0x9CFFB0, 0x020A04},
    // Amber monitor.
    {"AMBER",        0x0C0700, 0x221500, 0x3A2400, 0x2A1A00, 0xFFB000, 0x9A6A10, 0x4A3208, 0xFFD060, 0xFFFFFF,
                     0x4A3000, 0xFFC840, 0xFFB000, 0xFFE0A0, 0xFFB000, 0xFFD060, 0xFFF0C0, 0xFFFFFF, 0xFFD060, 0x0C0700},
};
// clang-format on
constexpr int kCount = int(sizeof kThemes / sizeof kThemes[0]);
int gCurrent = -1;

struct Init {
    Init() { applyTheme(0); }
} gInit;

}  // namespace

int themeCount() { return kCount; }
const Palette& theme(int i) { return kThemes[i >= 0 && i < kCount ? i : 0]; }
int currentTheme() { return gCurrent; }

void applyTheme(int i) {
    if (i < 0 || i >= kCount) i = 0;
    const Palette& p = kThemes[i];
    col::kBg = p.bg;
    col::kPanel = p.panel;
    col::kPanelHi = p.panelHi;
    col::kRowHi = p.rowHi;
    col::kText = p.text;
    col::kDim = p.dim;
    col::kFaint = p.faint;
    col::kCursor = p.cursor;
    col::kEdit = p.edit;
    col::kPlayRow = p.playRow;
    col::kNote = p.note;
    col::kInst = p.inst;
    col::kCmd = p.cmd;
    col::kSid1 = p.sid1;
    col::kSid2 = p.sid2;
    col::kWarn = p.warn;
    col::kBad = p.bad;
    col::kGood = p.good;
    col::kOnCursor = p.onCursor;
    gCurrent = i;
}

}  // namespace ui
}  // namespace sidt
