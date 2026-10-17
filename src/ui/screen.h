// SIDTracker -- screen interface and shared drawing helpers.
#pragma once

#include <cstdint>

#include "hal_api.h"
#include "ui/gfx.h"

namespace sidt {
namespace ui {

class Ui;

// Palette (our own; 0x00RRGGBB).
namespace col {
// The active theme (ui/palette.h: applyTheme).
extern uint32_t kBg, kPanel, kPanelHi, kRowHi, kText, kDim, kFaint, kCursor, kEdit, kPlayRow, kNote, kInst, kCmd,
    kSid1, kSid2, kWarn, kBad, kGood,
    kOnCursor;   // text on a kCursor fill (message pop-ups)
}  // namespace col

// Layout (800 x 480; ROM font 18 is the 8 x 16 monospace grid font).
constexpr int kTitleH = 28;
constexpr int kBarY = 426;
constexpr int kContentY = kTitleH + 4;
constexpr uint8_t kMono = 18;
constexpr uint8_t kSmall = 26;
constexpr uint8_t kText = 27;
constexpr uint8_t kLarge = 28;
constexpr uint8_t kTitleFont = 29;

class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* title() const = 0;
    virtual void enter() {}
    virtual void draw(ot::Gfx& g) = 0;
    // Bottom-bar labels for F1..F8 (null = empty).
    virtual void labels(const char* out[8]) { (void)out; }
    // Button press / release; return true when consumed (else Ui handles it).
    virtual bool button(uint8_t id, bool down) { (void)id; (void)down; return false; }
    // Pads: return true when consumed (else they audition notes).
    virtual bool pad(int p, bool down, uint8_t vel) { (void)p; (void)down; (void)vel; return false; }
    virtual void encoder(int delta) { (void)delta; }
    // Pad LED colours (0x00RRGGBB) for the screen; false = default keyboard.
    virtual bool padColour(int p, uint32_t& rgb) { (void)p; (void)rgb; return false; }
    // Redraw continuously (meters, playback); false = only after input.
    virtual bool animated() const { return false; }

    void attach(Ui& ui) { ui_ = &ui; }

protected:
    Ui& ui() { return *ui_; }
    Ui* ui_ = nullptr;
};

// Helpers.
// One colour layer of ROM-font-18 glyphs in a single BEGIN(BITMAPS): one
// VERTEX2II word per visible glyph. CMD_TEXT costs several words per call,
// and a full grid of per-field CMD_TEXTs overflows the FT81x's 2048-word
// display list (tests/app_test.cpp display_list_fits_with_a_full_pattern).
// VERTEX2II x is 9 bits: glyphs at x >= 512 go through VERTEX_TRANSLATE_X.
class GlyphLayer {
public:
    GlyphLayer(ot::Gfx& g, uint32_t colour) : d_(g.display()) {
        g.color(colour);
        d_.begin(ot::IDisplay::kBitmaps);
    }
    ~GlyphLayer() {
        if (base_) d_.vertexTranslateX(0);
        d_.end();
    }
    void put(int x, int y, const char* s) {
        for (int i = 0; s[i]; ++i) {
            if (s[i] == ' ') continue;
            const int px = x + i * 8;
            const int want = px < 512 ? 0 : 400;
            if (want != base_) {
                base_ = want;
                d_.vertexTranslateX(int32_t(want) * 16);
            }
            d_.vertex2ii(uint16_t(px - base_), uint16_t(y), kMono, uint8_t(s[i]));
        }
    }

private:
    ot::IDisplay& d_;
    int base_ = 0;
};

char hexDigit(int v);
void hex2(char* out, uint8_t v);              // "3F"
// A labelled list row "LABEL ........ VALUE" with selection highlight.
void listRow(ot::Gfx& g, int x, int y, int w, const char* label, const char* value, bool selected,
             uint32_t valueColour = col::kText);
// Value bar 0..1.
void meter(ot::Gfx& g, int x, int y, int w, int h, float frac, uint32_t c);

}  // namespace ui
}  // namespace sidt
