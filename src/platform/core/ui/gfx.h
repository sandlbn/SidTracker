// Thin drawing helpers over IDisplay (FT81x display list, spec 05 §3).
//
// Text uses the FT81x ROM fonts (part of the controller, spec 05 §3.6 lists
// the vendor's own fonts which we do not copy). Screens pass a *logical* font
// role; Gfx::text resolves it to a ROM font handle through the active font
// set (config "Font", spec 05 §3.6: two sets, applied at boot and on change).
//
//   role         set 0 "Set 1" (L4 antialiased)   set 1 "Set 2" (L1 bitmap)
//   kFontMono    18 (8x16 fixed)                   18 (grid cell, never changes)
//   kFontSmall   26 (h16)                          21 (h17)
//   kFontText    27 (h20)                          22 (h20)
//   kFontLarge   28 (h25)                          23 (h22)
//   kFontTitle   29 (h28)                          24 (h29)
//
// Set 1 keeps the heights of set 0 within a few pixels so every screen layout
// fits unchanged (stock set "New" is the wider/taller one; README differences).
#pragma once

#include <cstdarg>
#include <cstdint>

#include "hal_api.h"
#include "ui/theme.h"

namespace ot {

constexpr uint8_t kFontMono = 18;
constexpr uint8_t kFontSmall = 26;
constexpr uint8_t kFontText = 27;
constexpr uint8_t kFontLarge = 28;
constexpr uint8_t kFontTitle = 29;
constexpr uint8_t kFontSetCount = 2;

// Stock font roles (spec 20 §2.2 / §2.5): fonts 1..5 of the active set
// (config "Display font": 0 Original, 1 New). Set-0 cell / advance:
//   kFont1  10x18, 8 px mono   list rows, step numbers, small values
//   kFont2  10x22, 10 px mono  pattern grid cells (11-px pitch for "--")
//   kFont3  10x19, 6..9 px     bottom bar labels, panel / popup titles
//   kFont4  12x22, 10 px bold  title bar, track header tabs, column headers
//   kFont5   8x16, 7 px mono   text boxes, help text
//   kFontIcons                 icon atlas (Gfx::icon)
// With custom fonts bound (applyFontSet returned true) they resolve to
// IDisplay::kFontHandleBase + 0..4 / kIconHandle. Fallback (today, ROM fonts
// only): kFont1 -> kFontSmall, kFont2 -> kFontMono, kFont3 -> kFontText,
// kFont4 -> kFontLarge, kFont5 / kFontIcons -> kFontSmall, then the set-1
// ROM substitution below. Role values 1..6 are not ROM handles (16..31).
constexpr uint8_t kFont1 = 1;
constexpr uint8_t kFont2 = 2;
constexpr uint8_t kFont3 = 3;
constexpr uint8_t kFont4 = 4;
constexpr uint8_t kFont5 = 5;
constexpr uint8_t kFontIcons = 6;

// Active font set (config "Font", 0..1; out-of-range -> 0). Global like theme().
void setFontSet(uint8_t set);
uint8_t fontSet();
// ROM font handle for a logical font role in the active set (or `set`).
// Values that are not a role are passed through unchanged.
uint8_t romFontFor(uint8_t role);
uint8_t romFontFor(uint8_t role, uint8_t set);

// Select font set `set` (setFontSet) and ask the display to bind its custom
// fonts (IDisplay::bindFontSet); `d` may be null. Returns customFontsBound().
bool applyFontSet(IDisplay* d, uint8_t set);
// True after applyFontSet bound custom fonts: kFont1..kFontIcons then name
// bitmap handles and glyphRun draws VERTEX2II glyphs.
bool customFontsBound();

// Pen advance of `s` (or its first `len` chars, len < 0 = all) in pixels for a
// font role or ROM handle. ROM fonts: the FT81x ROM advance tables (the
// simulator's look-alikes use the same widths). Unknown handles: 8 px/char.
int textWidth(uint8_t font, const char* s, int len = -1);
// Cell height in pixels of a font role or ROM handle.
int fontHeight(uint8_t font);

// Icons drawn by Gfx::icon in a kIconSize x kIconSize box (top-left x, y):
// with custom fonts bound, cell `id` of the generated icon sheet on
// kIconHandle (our own drawings, tools/fonts/gen_fonts.py); fallback: simple
// primitives (stock icon art is never copied, spec 20 §5).
enum IconId : uint8_t {
    kIconPlay,
    kIconStop,
    kIconPause,
    kIconRec,
    kIconLoop,
    kIconArrowUp,     // ▲
    kIconArrowDown,   // ▼
    kIconArrowLeft,   // ◀
    kIconArrowRight,  // ▶
    kIconBattery,
    kIconFolder,
    kIconCount
};
constexpr int kIconSize = 12;

constexpr int kMonoW = 8;     // ROM font 18 cell width
constexpr int kMonoH = 16;

constexpr int kScreenW = 800;
constexpr int kScreenH = 480;
constexpr int kTitleH = 28;          // title strip
constexpr int kBarY = 424;           // bottom labels (spec 05 §2.2)
constexpr int kBarLabelW = 94;
constexpr int kBarLabelH = 55;

class Gfx {
public:
    explicit Gfx(IDisplay& d) : d_(d) {}
    IDisplay& display() { return d_; }

    void beginFrame(uint32_t bg);
    void endFrame();

    void color(uint32_t c);
    void alpha(uint8_t a) { d_.colorA(a); }
    void fill(int x, int y, int w, int h, uint32_t c);
    void frame(int x, int y, int w, int h, uint32_t c, int lw = 1);
    void hline(int x, int y, int w, uint32_t c);
    void vline(int x, int y, int h, uint32_t c);
    void line(int x0, int y0, int x1, int y1, uint32_t c);
    void text(int x, int y, uint8_t font, uint32_t c, const char* s, uint16_t opt = 0);
    void textf(int x, int y, uint8_t font, uint32_t c, uint16_t opt, const char* fmt, ...)
        __attribute__((format(printf, 7, 8)));
    // Glyph run (spec 20 §2.1): one VERTEX2II per glyph, top-left at (x, y),
    // pen advance from the font table or `advanceOverride` px when > 0 (the
    // grid's 11-px "--" rule, §2.3); VERTEX_TRANSLATE_X keeps the 9-bit
    // VERTEX2II x in range from x >= 512 on. Fallback without custom fonts:
    // the CMD_TEXT path (one CMD_TEXT per glyph when advanceOverride > 0).
    void glyphRun(int x, int y, uint8_t font, const char* s, uint32_t colour, int advanceOverride = 0);
    int textWidth(uint8_t font, const char* s) const { return ot::textWidth(font, s); }
    // Text turned by quarterTurns x 90 degrees counter-clockwise (1 = reads
    // bottom to top; stock style bit7 "vertical text"). (x, y) is the
    // top-left of the turned box (cell height wide for 1 / 3), adjusted by
    // kOptCenterX / kOptRightX / kOptCenterY. Custom fonts: SAVE_CONTEXT,
    // bitmapSize + BITMAP_TRANSFORM, one VERTEX2II per glyph, RESTORE_CONTEXT.
    // Fallback (ROM fonts): horizontal.
    void textRotated(int x, int y, uint8_t font, uint32_t c, const char* s, int quarterTurns = 1,
                     uint16_t opt = 0);
    // CMD_GRADIENT from c0 at (x0, y0) to c1 at (x1, y1) over the current
    // clip (spec 20 §1.5). Fallback displays: a solid fill.
    void gradient(int x0, int y0, int x1, int y1, uint32_t c0, uint32_t c1);
    // Box x, y, w, h filled with a gradient c0 -> c1, top to bottom (vertical)
    // or left to right: SAVE_CONTEXT, scissor, CMD_GRADIENT, RESTORE_CONTEXT.
    void gradientRect(int x, int y, int w, int h, uint32_t c0, uint32_t c1, bool vertical = true);
    // 0.5-px line (LINE_WIDTH 8, spec 20 §1.5); LINE_WIDTH returns to 16.
    void hairline(int x0, int y0, int x1, int y1, uint32_t c);
    // Filled rectangle x, y, w, h with corner radius r px (RECTS with
    // LINE_WIDTH r * 16); r <= 1 is a plain fill. LINE_WIDTH returns to 16.
    void roundRect(int x, int y, int w, int h, int r, uint32_t c);
    // Icon `id` in the kIconSize box at top-left (x, y).
    void icon(int x, int y, IconId id, uint32_t colour);

    void clip(int x, int y, int w, int h);
    void noClip();
    // Horizontal/vertical value bar with frame; frac 0..1. bipolar: fill from the middle.
    void vbar(int x, int y, int w, int h, float frac, uint32_t fillC, uint32_t frameC, bool bipolar = false);
    void hbar(int x, int y, int w, int h, float frac, uint32_t fillC, uint32_t frameC);

    // Batched rectangles: one BEGIN/END for many rects (2 vertices each).
    void rectsBegin(uint32_t c);
    void rectsAdd(int x, int y, int w, int h);
    void rectsEnd();

    // Approximate display-list words emitted this frame (FT81x RAM_DL is 2048 words).
    uint32_t dlWords() const { return words_; }

private:
    IDisplay& d_;
    uint32_t cur_ = 0xFFFFFFFF;
    uint32_t words_ = 0;
};

}  // namespace ot
