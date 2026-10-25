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
        case ScreenId::kSidPlayer: return &sidPlayer_;
        case ScreenId::kExtended: return &extended_;
        default: return &pattern_;
    }
}

void Ui::show(ScreenId id) {
    if (current_ == ScreenId::kSidPlayer && id != ScreenId::kSidPlayer) app_->sidStop();   // the tracker gets the chips back
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

bool Ui::confirm(Screen* s, uint8_t action, const char* question, const char* ok) {
    if (confirmed_ == action) return true;
    ask_.open = true;
    std::snprintf(ask_.text, sizeof ask_.text, "%s", question);
    ask_.ok = ok;
    ask_.target = s;
    ask_.action = action;
    dirty_ = true;
    return false;
}

void Ui::drawPicker(ot::Gfx& g) {
    const Song& sg = song();
    bool used[kInstrumentCount] = {};
    for (int i = 0; i < sg.orderLen; ++i) {
        const Pattern& p = sg.pattern(sg.order[i].pattern);
        for (int r = 0; r < p.rows; ++r)
            for (const Cell& c : p.cells[r])
                if (c.inst < kInstrumentCount) used[c.inst] = true;
    }
    const int base = pick_.slot > 48 ? 48 : 0;
    g.text(16, kContentY + 6, kMono, col::kDim,
           pick_.sticky ? "PICK INSTRUMENT  pads / jog: select and play   Instr: choose   CLOSE: keep"
                        : "PICK INSTRUMENT  pads / jog: select and play   release Instr: choose   Insert: write");
    const int cw = 66, chh = 90, x0 = 4, y0 = kContentY + 30;
    char buf[12];
    for (int p = 0; p < 48; ++p) {
        const int slot = base + p + 1;
        if (slot >= kInstrumentCount) break;
        const int x = x0 + (p % ot::kPadCols) * cw, y = y0 + (p / ot::kPadCols) * chh;
        const bool sel = slot == pick_.slot, cur = slot == inst;
        const Instrument& in = sg.inst[slot];
        const bool live = used[slot] || !in.empty();
        g.fill(x, y, cw - 4, chh - 4, sel ? col::kCursor : (live ? col::kPanelHi : col::kPanel));
        if (cur && !sel) g.frame(x, y, cw - 4, chh - 4, col::kInst, 1);
        std::snprintf(buf, sizeof buf, "%02X", unsigned(slot));
        const uint32_t tc = sel ? col::kOnCursor : (live ? col::kText : col::kFaint);
        g.text(x + 4, y + 4, kMono, tc, buf);
        if (!live) continue;   // an unused init slot: the number is enough
        // Name in two lines of 7, broken at a space where there is one.
        char a[8] = {}, bname[8] = {};
        const char* sp = std::strchr(in.name, ' ');
        const int len = int(std::strlen(in.name));
        int k = sp && sp - in.name <= 7 ? int(sp - in.name) : 7;
        if (k > len) k = len;
        std::snprintf(a, sizeof a, "%.*s", k, in.name);
        const char* rest = in.name + k;
        while (*rest == ' ') ++rest;
        std::snprintf(bname, sizeof bname, "%.7s", rest);
        g.text(x + 4, y + 30, kSmall, tc, a);
        if (bname[0]) g.text(x + 4, y + 48, kSmall, tc, bname);
    }
}

void Ui::drawDialog(ot::Gfx& g) {
    const int w = 560, h = 110, x = 400 - w / 2, y = kBarY - h - 16;   // below the report panels
    g.fill(x, y, w, h, col::kPanel);
    g.frame(x, y, w, h, col::kCursor, 2);
    g.text(400, y + 40, kMono, col::kText, ask_.text, ot::IDisplay::kOptCenter);
    char hint[48];
    std::snprintf(hint, sizeof hint, "%s or CANCEL (buttons below)", ask_.ok);
    g.text(400, y + 82, kMono, col::kDim, hint, ot::IDisplay::kOptCenter);
}

void Ui::status(const char* text, uint32_t ms) {
    std::snprintf(status_, sizeof status_, "%s", text);
    statusUntil_ = now_ + ms;
    dirty_ = true;
}

void Ui::undoRedo(bool redo) {
    char msg[32];
    if (redo ? app_->redo() : app_->undo())
        std::snprintf(msg, sizeof msg, "%s (%d left)", redo ? "Redo" : "Undo", redo ? app_->redoSteps() : app_->undoSteps());
    else
        std::snprintf(msg, sizeof msg, "Nothing to %s", redo ? "redo" : "undo");
    status(msg);
}

void Ui::songEdited() {
    instrument_.forget();
    dirty_ = true;
}

void Ui::songChanged() {
    pattern = 0;
    orderPos = 0;
    inst = 1;
    channel = 0;
    edit = false;
    pattern_.reset();
    instrument_.forget();   // design cache, users line and a preset being tried
    dirty_ = true;
}

uint8_t Ui::padNote(int pad) const {
    if (pad < 0 || pad >= ot::kPadCount) return 0;
    const int r = pad / ot::kPadCols, c = pad % ot::kPadCols;
    const int semi = (octave + (ot::kPadRows - 1 - r)) * 12 + c;   // bottom row = octave
    return semi < kNoteCount ? uint8_t(semi + 1) : 0;
}

void Ui::auditionNote(int pad, uint8_t instrument, uint8_t note) {
    if (pad < 0 || pad >= ot::kPadCount) return;
    const int ch = channel < kChannels ? channel : 0;
    app_->engine.noteOn(ch, note, instrument);
    padHeld_[pad] = note;
    padChannel_[pad] = ch;
}

void Ui::auditionOn(int pad, uint8_t instrument) {
    const uint8_t n = padNote(pad);
    if (!n) return;
    app_->engine.noteOn(channel, n, instrument);
    padHeld_[pad] = n;
    padChannel_[pad] = channel;
}

void Ui::midiNote(uint8_t m, bool on) {
    if (m > 127) return;
    if (!on) {
        const int t = midiHeld_[m] - 1;
        midiHeld_[m] = 0;
        if (t < 0) return;
        for (int k = 0; k < 128; ++k)
            if (midiHeld_[k] == t + 1) return;   // another key still holds the track
        app_->engine.noteOff(t);
        return;
    }
    if (m < 12 || m >= 12 + kNoteCount) return;
    const uint8_t n = uint8_t(m - 11);   // MIDI 12 = C-0 = note value 1
    if (channel >= 0 && channel < kChannels) {
        app_->engine.noteOn(channel, n, inst);
        midiHeld_[m] = int8_t(channel + 1);
    }
    if (screen(current_)->midiNote(n)) dirty_ = true;
}

void Ui::retriggerHeld(uint8_t instrument) {
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p]) app_->engine.noteOn(padChannel_[p], padHeld_[p], instrument);
}

void Ui::preview(uint8_t instrument) {
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p]) {
            retriggerHeld(instrument);
            return;
        }
    const int ch = channel < kChannels ? channel : 0;   // drum column: track 1
    if (previewChannel_ >= 0 && previewChannel_ != ch) app_->engine.noteOff(previewChannel_);
    previewChannel_ = ch;
    app_->engine.noteOn(ch, uint8_t(1 + (octave + 1) * 12), instrument);
    previewUntil_ = now_ + 300;
}

void Ui::auditionOff(int pad) {
    if (pad < 0 || pad >= ot::kPadCount || !padHeld_[pad]) return;
    padHeld_[pad] = 0;
    for (int p = 0; p < ot::kPadCount; ++p)
        if (padHeld_[p] && padChannel_[p] == padChannel_[pad]) return;   // another pad still holds it
    app_->engine.noteOff(padChannel_[pad]);
}

namespace {
// Pad keyboard rows, top row of pads first (pad = row * 12 + column).
const char kKeys[4][13] = {"1234567890-_", "QWERTYUIOP()", "ASDFGHJKL!?.", "ZXCVBNM,'/+ "};
}  // namespace

void Ui::keyboard(Screen* s, uint8_t action, const char* title, const char* initial, bool fileName) {
    kb_.open = true;
    kb_.done = false;
    kb_.fileName = fileName;
    kb_.target = s;
    kb_.action = action;
    std::snprintf(kb_.title, sizeof kb_.title, "%s", title);
    std::snprintf(kb_.text, sizeof kb_.text, "%s", initial);
    dirty_ = true;
}

void Ui::keyboardInput(const ot::InputEvent& ev) {
    const size_t len = std::strlen(kb_.text);
    if (ev.type == ot::InputEvent::kButtonUp && ev.id == b::Shift) shift = false;
    if (ev.type == ot::InputEvent::kPadDown && ev.id < 48) {
        const char c = kKeys[ev.id / 12][ev.id % 12];
        const bool ok = !kb_.fileName || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok && len < sizeof kb_.text - 1) {
            kb_.text[len] = c;
            kb_.text[len + 1] = 0;
        }
        return;
    }
    if (ev.type != ot::InputEvent::kButtonDown) return;
    switch (ev.id) {
        case b::Shift: shift = true; return;
        case b::Delete:
            if (len) kb_.text[len - 1] = 0;
            return;
        case b::F8:
        case b::EncPush:
            if (!len) return;
            kb_.open = false;
            kb_.done = true;   // not a dialog confirmation: the screen may still ask
            if (kb_.target) kb_.target->button(kb_.action, true);
            kb_.done = false;
            return;
        default:
            if (ev.id == b::F7 || b::isModuleKey(ev.id)) {
                kb_.open = false;
                status("Cancelled");
            }
            return;
    }
}

void Ui::drawKeyboard(ot::Gfx& g) {
    g.text(16, kContentY + 6, kMono, col::kDim, kb_.title);
    char line[kNameLen + 4];
    std::snprintf(line, sizeof line, "%s_", kb_.text);
    g.fill(16, kContentY + 26, 768, 34, col::kPanel);
    g.text(28, kContentY + 34, kLarge, col::kText, line);
    const int cw = 66, chh = 70, x0 = 4, y0 = kContentY + 70;
    char k[2] = {};
    for (int p = 0; p < 48; ++p) {
        const char c = kKeys[p / 12][p % 12];
        const bool ok = !kb_.fileName || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        const int x = x0 + (p % 12) * cw, y = y0 + (p / 12) * chh;
        g.fill(x, y, cw - 4, chh - 4, ok ? col::kPanelHi : col::kPanel);
        k[0] = c;
        g.text(x + cw / 2 - 2, y + chh / 2 - 2, kLarge, ok ? col::kText : col::kFaint, c == ' ' ? "SP" : k,
               ot::IDisplay::kOptCenter);
    }
    g.text(16, kBarY - 22, kMono, col::kDim, "pads: type   Delete: back   then OK or CANCEL below");
}

void Ui::openPicker(bool sticky) {
    pick_.open = true;
    pick_.sticky = sticky;
    pick_.slot = inst;
    dirty_ = true;
}

bool Ui::pickerInput(const ot::InputEvent& ev) {
    auto move = [&](int d) { pick_.slot = uint8_t(((pick_.slot - 1 + d) % 63 + 63) % 63 + 1); };
    auto choose = [&]() {
        inst = pick_.slot;
        pick_.open = false;
        // As on the stock Tracker: in edit mode the chosen instrument is
        // written into the pattern step too.
        if (current_ == ScreenId::kPattern && edit) pattern_.writeInstrument(inst);
        char m[40];
        std::snprintf(m, sizeof m, "Instrument %02X %s", unsigned(inst), song().inst[inst].name);
        status(m);
    };
    switch (ev.type) {
        case ot::InputEvent::kButtonDown:
            if (ev.id == b::Shift) {
                shift = true;
                return true;
            }
            switch (ev.id) {
                case b::Left: move(-1); return true;
                case b::Right: move(1); return true;
                case b::Up: move(-ot::kPadCols); return true;
                case b::Down: move(ot::kPadCols); return true;
                case b::F8:   // the other page
                    pick_.slot = uint8_t(pick_.slot <= 48 ? (pick_.slot + 48 < kInstrumentCount ? pick_.slot + 48 : 63)
                                                          : pick_.slot - 48);
                    return true;
                case b::F7: pick_.open = false; return true;   // CLOSE: keep the instrument
                case b::Instr:
                case b::EncPush:
                    if (pick_.sticky) choose();
                    return true;
                case b::Insert:
                    if (current_ == ScreenId::kPattern && edit) {
                        pattern_.writeInstrument(pick_.slot);
                        inst = pick_.slot;
                    }
                    return true;
                default: return true;
            }
        case ot::InputEvent::kButtonUp:
            if (ev.id == b::Shift) shift = false;
            if (ev.id == b::Instr) {
                pick_.instrHeld = false;
                if (!pick_.sticky) choose();
            }
            return true;
        case ot::InputEvent::kPadDown: {
            const int slot = (pick_.slot > 48 ? 48 : 0) + ev.id + 1;
            if (slot >= 1 && slot < kInstrumentCount) {
                pick_.slot = uint8_t(slot);
                // Every pad plays the same C (octave + 1): compare sounds, not pitches.
                const uint8_t n = uint8_t(1 + (octave + 1) * 12);
                const int ch = channel < kChannels ? channel : 0;   // drum column: track 1
                app_->engine.noteOn(ch, n, pick_.slot);
                padHeld_[ev.id] = n;
                padChannel_[ev.id] = ch;
                if (ev.id < 64) {
                    padsDown_ |= 1ull << ev.id;
                    pickerPads_ |= 1ull << ev.id;   // released after the picker closes: still ours
                }
            }
            return true;
        }
        case ot::InputEvent::kPadUp:
            if (ev.id < 64) {
                padsDown_ &= ~(1ull << ev.id);
                pickerPads_ &= ~(1ull << ev.id);
            }
            auditionOff(ev.id);
            return true;
        case ot::InputEvent::kEncoder:
            move(ev.delta);
            return true;
        default: return true;
    }
}

void Ui::handle(const ot::InputEvent& ev) {
    dirty_ = true;
    Screen& s = *screen(current_);
    if (kb_.open) {
        keyboardInput(ev);
        return;
    }
    if (pick_.open && !ask_.open) {
        pickerInput(ev);
        return;
    }
    if (ev.id == b::Instr && (ev.type == ot::InputEvent::kButtonDown || ev.type == ot::InputEvent::kButtonUp)) {
        const bool down = ev.type == ot::InputEvent::kButtonDown;
        pick_.instrHeld = down;
        pick_.instrDownAt = down ? now_ : 0;
        if (down && current_ == ScreenId::kInstrument && !help_ && !ask_.open && !kb_.open) {   // a tap opens it here
            openPicker(true);
            return;
        }
    }
    if (ask_.open) {   // the dialog takes every input
        if (ev.type == ot::InputEvent::kButtonDown && ev.id == b::Shift) shift = true;
        if (ev.type == ot::InputEvent::kButtonUp && ev.id == b::Shift) shift = false;
        if (ev.type == ot::InputEvent::kPadUp) {   // a pad held before the question: release it
            if (ev.id < 64) padsDown_ &= ~(1ull << ev.id);
            auditionOff(ev.id);
            return;
        }
        if (ev.type != ot::InputEvent::kButtonDown || ev.id == b::Shift) return;
        // The key that opened the dialog auto-repeats while held (Delete,
        // Insert, jog push): that is not an answer.
        if (ev.id == ask_.action && b::autoRepeats(ev.id)) return;
        ask_.open = false;
        if (ev.id == b::F8 && ask_.target) {
            const bool sh = shift;
            shift = false;   // the screen sees the plain key it asked about
            confirmed_ = ask_.action;
            ask_.target->button(ask_.action, true);
            confirmed_ = -1;
            shift = sh;
        } else {
            status("Cancelled");
        }
        return;
    }
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
                    if (shift) undoRedo(false);
                    else edit = !edit;
                    break;
                case b::SampleRecorder:
                    if (shift) undoRedo(true);
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
            if (!down && ev.id < 64 && ((pickerPads_ >> ev.id) & 1)) {   // pressed inside the picker
                pickerPads_ &= ~(1ull << ev.id);
                auditionOff(ev.id);
                return;
            }
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
    if (app_->dirty()) g.text(138, 6, kText, col::kWarn, "*");   // unsaved changes (UX plan P2.4)
    char buf[96];
    if (now_ < statusUntil_) {
        g.text(262, 6, kText, col::kWarn, status_);
    } else if (pl.playing()) {
        std::snprintf(buf, sizeof buf, "%s POS %02X PAT %02X ROW %02X", pl.mode() == Player::Mode::kSong ? "SONG" : "LOOP",
                      unsigned(pl.orderPos()), unsigned(pl.patternIndex()), unsigned(pl.row()));
        g.text(262, 6, kText, col::kGood, buf);
    } else {
        g.text(262, 6, kText, col::kDim, "STOPPED");
    }
    std::snprintf(buf, sizeof buf, sg.multiSpeed() > 1 ? "BPM %u SPD %u x%u" : "BPM %u SPD %u", unsigned(sg.bpm),
                  unsigned(pl.playing() ? pl.speed() : sg.speed), unsigned(sg.multiSpeed()));
    g.text(505, 6, kText, col::kText, buf);
    // Which instrument the pads play, and on which track (UX plan P1.2).
    std::snprintf(buf, sizeof buf, "INS %02X T%d", unsigned(inst), channel < kChannels ? channel + 1 : 1);
    g.text(672, 6, kText, col::kInst, buf);
    if (edit) g.fill(784, 6, 10, 16, col::kEdit);
    if (app_->engine.stress()) g.text(690, 6, kText, col::kWarn, "STRESS");
    // Bottom bar: F1..F8 (a dialog shows only its own choices).
    const char* labels[8] = {};
    if (kb_.open) {
        labels[6] = "CANCEL";
        labels[7] = "OK";
    } else if (pick_.open) {
        labels[6] = "CLOSE";
        labels[7] = pick_.slot <= 48 ? "49-63 >" : "< 01-48";
    } else if (ask_.open) {
        labels[6] = "CANCEL";
        labels[7] = ask_.ok;
    } else {
        s.labels(labels);
    }
    for (int i = 0; i < 8; ++i) {
        const int x = i * 100 + 3;
        g.fill(x, kBarY, 94, 50, labels[i] ? col::kPanel : col::kBg);
        if (labels[i]) g.text(x + 47, kBarY + 25, kSmall, col::kText, labels[i], ot::IDisplay::kOptCenter);
    }
    if (now_ < msgUntil_) {
        const int w = 8 * int(std::strlen(msg_)) + 32;
        g.fill(400 - w / 2, kBarY - 40, w, 30, col::kPanelHi);
        g.frame(400 - w / 2, kBarY - 40, w, 30, col::kCursor, 1);
        g.text(400, kBarY - 25, kMono, col::kText, msg_, ot::IDisplay::kOptCenter);
    }
}

void Ui::drawHelp(ot::Gfx& g) {
    static const char* const kLines[] = {
        "KEYS  (any key closes)",
        "Pattern: pattern grid      Instr. Params: instrument",
        "Performance: perform       Song: order list",
        "Instr. Params again: next page (Shift + a bottom button: page)",
        "Config: settings           File: projects",
        "Master: track mixer (again: settings; Shift: benchmark)",
        "File > SID PLAYER: .sid files on the card",
        "Sample Loader: effects, drum kit, LFOs, pan",
        "",
        "Play: start / stop (Shift: loop the pattern)",
        "Rec: edit mode   Shift+Rec: undo   Shift+Sample Rec: redo",
        "Pads: notes, bottom row = octave (OCT -/+ on Pattern)",
        "      with Rec on hex fields: the first 16 pads (lit red) type 0-F",
        "Jog: value under the cursor (Shift: coarse)   hold Instr: picker",
        "Shift + arrows: page / track jumps",
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
    if (previewChannel_ >= 0 && int32_t(nowMs - previewUntil_) >= 0) {
        bool held = false;
        for (int p = 0; p < ot::kPadCount; ++p) held = held || (padHeld_[p] && padChannel_[p] == previewChannel_);
        if (!held) app_->engine.noteOff(previewChannel_);
        previewChannel_ = -1;
    }
    if (pick_.instrHeld && !pick_.open && !ask_.open && !help_ && nowMs - pick_.instrDownAt >= 180) {   // the stock hold time
        openPicker(false);   // held: choose on release
    }
    bench_.poll(nowMs);
    if (nowMs - lastLeds_ >= 40) {
        lastLeds_ = nowMs;
        updateLeds();
    }
    Screen& s = *screen(current_);
    const bool animate = s.animated() || app_->engine.player().playing() || nowMs < msgUntil_ + 40 ||
                         nowMs < statusUntil_ + 40;
    const uint32_t period = dirty_ ? 16u : 40u;
    if (!(dirty_ || animate) || nowMs - lastFrame_ < period) return;
    lastFrame_ = nowMs;
    dirty_ = false;
    ot::Gfx g(*app_->platform().display);
    g.beginFrame(col::kBg);
    // Help replaces the screen: drawn over a full pattern it came within a
    // few words of the FT81x display-list limit (UX plan P0.4).
    // Help and the picker replace the screen (display-list budget).
    if (kb_.open) drawKeyboard(g);
    else if (pick_.open) drawPicker(g);
    else if (!help_) s.draw(g);
    drawChrome(g, s);
    if (help_) drawHelp(g);
    if (ask_.open) drawDialog(g);
    g.endFrame();
    ++frames_;
}

}  // namespace ui
}  // namespace sidt
