// SIDTracker -- user interface (ui.h).
#include "ui/ui.h"

#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace b = ot::btn;

// --- helpers -------------------------------------------------------------------

char hexDigit(int v) { return "0123456789ABCDEF"[v & 15]; }

void hex2(char* out, uint8_t v) {
    out[0] = hexDigit(v >> 4);
    out[1] = hexDigit(v);
}

void listRow(ot::Gfx& g, int x, int y, int w, const char* label, const char* value, bool selected,
             uint32_t valueColour) {
    g.fill(x, y - 2, w, 22, selected ? col::kPanelHi : col::kPanel);
    if (selected) g.fill(x, y - 2, 3, 22, col::kCursor);
    g.text(x + 10, y + 1, kMono, selected ? col::kText : col::kDim, label);
    g.text(x + w - 8, y + 1, kMono, valueColour, value, ot::IDisplay::kOptRightX);
}

void meter(ot::Gfx& g, int x, int y, int w, int h, float frac, uint32_t c) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    g.fill(x, y, w, h, col::kFaint);
    const int f = int(float(w) * frac);
    if (f > 0) g.fill(x, y, f, h, c);
}

// --- Ui ------------------------------------------------------------------------

void Ui::init(App& app) {
    app_ = &app;
    for (int i = 0; i < int(ScreenId::kCount); ++i) screen(ScreenId(i))->attach(*this);
    current_ = ScreenId::kPattern;
    screen(current_)->enter();
    dirty_ = true;
}

Song& Ui::song() { return app_->song; }

Screen* Ui::screen(ScreenId id) {
    switch (id) {
        case ScreenId::kPattern: return &pattern_;
        case ScreenId::kInstrument: return &instrument_;
        case ScreenId::kPerform: return &perform_;
        case ScreenId::kSong: return &songScreen_;
        case ScreenId::kSettings: return &settings_;
        case ScreenId::kFile: return &file_;
        case ScreenId::kBench: return &bench_;
        case ScreenId::kMaster: return &master_;
        case ScreenId::kExtended: return &extended_;
        default: return &pattern_;
    }
}

void Ui::show(ScreenId id) {
    current_ = id;
    help_ = false;
    screen(id)->enter();
    dirty_ = true;
}

void Ui::message(const char* text, uint32_t ms) {
    std::snprintf(msg_, sizeof msg_, "%s", text);
    msgUntil_ = now_ + ms;
    dirty_ = true;
}

void Ui::songChanged() {
    pattern = 0;
    orderPos = 0;
    inst = 1;
    channel = 0;
    edit = false;
    pattern_.reset();
    dirty_ = true;
}

uint8_t Ui::padNote(int pad) const {
    if (pad < 0 || pad >= ot::kPadCount) return 0;
    const int r = pad / ot::kPadCols, c = pad % ot::kPadCols;
    const int semi = (octave + (ot::kPadRows - 1 - r)) * 12 + c;   // bottom row = octave
    return semi < kNoteCount ? uint8_t(semi + 1) : 0;
}

void Ui::auditionOn(int pad, uint8_t instrument) {
    const uint8_t n = padNote(pad);
    if (!n) return;
    app_->engine.noteOn(channel, n, instrument);
    padHeld_[pad] = n;
    padChannel_[pad] = channel;
}

void Ui::retriggerHeld(uint8_t instrument) {
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p]) app_->engine.noteOn(padChannel_[p], padHeld_[p], instrument);
}

void Ui::auditionOff(int pad) {
    if (pad < 0 || pad >= ot::kPadCount || !padHeld_[pad]) return;
    padHeld_[pad] = 0;
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p] && padChannel_[p] == padChannel_[pad]) return;   // another pad still holds it
    app_->engine.noteOff(padChannel_[pad]);
}

void Ui::handle(const ot::InputEvent& ev) {
    dirty_ = true;
    Screen& s = *screen(current_);
    switch (ev.type) {
        case ot::InputEvent::kButtonDown:
        case ot::InputEvent::kButtonUp: {
            const bool down = ev.type == ot::InputEvent::kButtonDown;
            if (ev.id == b::Shift) {
                shift = down;
                return;
            }
            if (ev.id == b::Help) {
                if (down) help_ = !help_;
                return;
            }
            if (help_ && down) {
                help_ = false;
                return;
            }
            if (ev.id == b::Power) {
                // Shift + Power: screenshot (as stock, spec 05 §3.4). A long
                // press without Shift powers off in the HAL.
                if (down && shift) {
                    char file[40];
                    message(app_->screenshot(file, sizeof file) ? file : "Screenshot failed", 2000);
                }
                return;
            }
            if (s.button(ev.id, down) || !down) return;
            switch (ev.id) {
                case b::Pattern: show(ScreenId::kPattern); break;
                case b::InstrParams: show(ScreenId::kInstrument); break;
                case b::Performance: show(ScreenId::kPerform); break;
                case b::Song: show(ScreenId::kSong); break;
                case b::Config: show(ScreenId::kSettings); break;
                case b::File: show(ScreenId::kFile); break;
                case b::Master: show(shift ? ScreenId::kBench : ScreenId::kMaster); break;
                case b::SampleLoader: show(ScreenId::kExtended); break;
                case b::Rec:
                    edit = !edit;
                    break;
                case b::Play: {
                    Engine& e = app_->engine;
                    if (e.player().playing()) {
                        e.stop();
                    } else if (shift) {
                        e.playPattern(pattern);
                    } else {
                        // Song from the position that holds the edited pattern.
                        int pos = -1;
                        const Song& sg = song();
                        if (orderPos < sg.orderLen && sg.order[orderPos].pattern == pattern) pos = orderPos;
                        for (int i = 0; pos < 0 && i < sg.orderLen; ++i)
                            if (sg.order[i].pattern == pattern) pos = i;
                        if (pos >= 0) e.playSong(uint8_t(pos));
                        else e.playPattern(pattern);
                    }
                    break;
                }
                default: break;
            }
            return;
        }
        case ot::InputEvent::kPadDown:
        case ot::InputEvent::kPadUp: {
            const bool down = ev.type == ot::InputEvent::kPadDown;
            if (ev.id < 64) padsDown_ = down ? (padsDown_ | (1ull << ev.id)) : (padsDown_ & ~(1ull << ev.id));
            if (s.pad(ev.id, down, ev.velocity)) return;
            if (down) auditionOn(ev.id, inst);
            else auditionOff(ev.id);
            return;
        }
        case ot::InputEvent::kEncoder:
            if (help_) return;
            s.encoder(ev.delta);
            return;
        default: return;
    }
}

void Ui::drawChrome(ot::Gfx& g, Screen& s) {
    const Song& sg = song();
    const Player& pl = app_->engine.player();
    g.fill(0, 0, 800, kTitleH, col::kPanel);
    g.text(8, 4, kLarge, col::kSid1, "SID");
    g.text(44, 4, kLarge, col::kSid2, "TRACKER");
    g.text(150, 6, kText, col::kText, s.title());
    char buf[96];
    if (pl.playing()) {
        std::snprintf(buf, sizeof buf, "%s POS %02X PAT %02X ROW %02X", pl.mode() == Player::Mode::kSong ? "SONG" : "LOOP",
                      unsigned(pl.orderPos()), unsigned(pl.patternIndex()), unsigned(pl.row()));
        g.text(300, 6, kText, col::kGood, buf);
    } else {
        g.text(300, 6, kText, col::kDim, "STOPPED");
    }
    std::snprintf(buf, sizeof buf, "BPM %u SPD %u", unsigned(sg.bpm), unsigned(pl.playing() ? pl.speed() : sg.speed));
    g.text(560, 6, kText, col::kText, buf);
    if (edit) g.fill(784, 6, 10, 16, col::kEdit);
    if (app_->engine.stress()) g.text(690, 6, kText, col::kWarn, "STRESS");
    // Bottom bar: F1..F8.
    const char* labels[8] = {};
    s.labels(labels);
    for (int i = 0; i < 8; ++i) {
        const int x = i * 100 + 3;
        g.fill(x, kBarY, 94, 50, labels[i] ? col::kPanel : col::kBg);
        if (labels[i]) g.text(x + 47, kBarY + 25, kSmall, col::kText, labels[i], ot::IDisplay::kOptCenter);
    }
    if (now_ < msgUntil_) {
        const int w = 8 * int(std::strlen(msg_)) + 32;
        g.fill(400 - w / 2, kBarY - 40, w, 30, col::kCursor);
        g.text(400, kBarY - 25, kMono, col::kOnCursor, msg_, ot::IDisplay::kOptCenter);
    }
}

void Ui::drawHelp(ot::Gfx& g) {
    static const char* const kLines[] = {
        "KEYS",
        "Pattern: pattern grid      Instr. Params: instrument",
        "Performance: perform       Song: order list",
        "Config: settings           File: projects",
        "Master: mixer, meters, CPU (Shift: benchmark)   Help: this page",
        "Sample Loader: effects, drum kit, LFOs, pan",
        "",
        "Play: start / stop (Shift: loop the pattern)",
        "Rec: edit mode (red cursor)",
        "Pads: notes, bottom row = octave (F3/F4 on Pattern)",
        "      with Rec on hex fields: the first 16 pads type 0-F",
        "Jog wheel: value under the cursor (Shift: coarse)",
        "Shift + arrows: page / track jumps",
        "",
        "any key closes",
    };
    g.fill(60, 40, 680, 370, col::kPanel);
    g.frame(60, 40, 680, 370, col::kCursor, 2);
    int y = 56;
    for (const char* l : kLines) {
        g.text(80, y, kMono, l == kLines[0] ? col::kWarn : col::kText, l);
        y += 22;
    }
}

void Ui::updateLeds() {
    ot::ILeds* leds = app_->platform().leds;
    if (!leds) return;
    static uint32_t shown[ot::kPadCount];
    static bool init = false;
    Screen& s = *screen(current_);
    bool changed = !init;
    for (int p = 0; p < ot::kPadCount && p < leds->padCount(); ++p) {
        uint32_t c;
        if (!s.padColour(p, c)) {
            const int semi = p % ot::kPadCols;
            const bool black = semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
            c = semi == 0 ? 0x002448 : (black ? 0x000008 : 0x101010);
        }
        if ((padsDown_ >> p) & 1) c = 0x00A0FF;
        if (c != shown[p] || !init) {
            shown[p] = c;
            leds->setPad(uint8_t(p), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c));
            changed = true;
        }
    }
    init = true;
    if (changed) leds->show();
}

void Ui::update(uint32_t nowMs) {
    now_ = nowMs;
    bench_.poll(nowMs);
    if (nowMs - lastLeds_ >= 40) {
        lastLeds_ = nowMs;
        updateLeds();
    }
    Screen& s = *screen(current_);
    const bool animate = s.animated() || app_->engine.player().playing() || nowMs < msgUntil_ + 40;
    const uint32_t period = dirty_ ? 16u : 40u;
    if (!(dirty_ || animate) || nowMs - lastFrame_ < period) return;
    lastFrame_ = nowMs;
    dirty_ = false;
    ot::Gfx g(*app_->platform().display);
    g.beginFrame(col::kBg);
    s.draw(g);
    drawChrome(g, s);
    if (help_) drawHelp(g);
    g.endFrame();
    ++frames_;
}

}  // namespace ui
}  // namespace sidt
