// SIDTracker -- Pattern screen (pattern_screen.h).
#include "ui/pattern/pattern_screen.h"

#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "tracker/commands/commands.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
constexpr int kGridX = 24;
constexpr int kChanW = 120;   // 15 grid characters per track
constexpr int kDrumCh = kChannels;                       // cursor "track" 6 = drum column
constexpr int kTracks = kChannels + 1;
constexpr int kDrumX = kGridX + kChannels * kChanW + 8;  // 2 hex digits
constexpr int kHeadY = kContentY + 2;
constexpr int kChipY = kHeadY + 22;
constexpr int kChanY = kChipY + 18;
constexpr int kRowY = kChanY + 20;
constexpr int kRowH = 16;
constexpr int kHelpY = kRowY + PatternScreen::kRows * kRowH + 8;
// Character offset of each cursor field inside "C-4 01 A03".
constexpr int kFieldChar[PatternScreen::kFields] = {0, 4, 5, 7, 8, 9};
constexpr int kFieldW[PatternScreen::kFields] = {3, 2, 1, 1, 1, 1};   // the instrument is one field
constexpr uint8_t kLengths[] = {16, 32, 48, 64, 96, 128};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

Pattern& PatternScreen::pat() { return ui().song().pattern(ui().pattern); }
Cell& PatternScreen::cell() {
    static Cell dummy;   // the drum column has no Cell
    if (row_ >= pat().rows) row_ = pat().rows - 1;   // the length may have shrunk (undo)
    return ui().channel < kChannels ? pat().cells[row_][ui().channel] : (dummy = Cell{});
}

void PatternScreen::edited() { ui().app().markDirty(); }

void PatternScreen::moveRow(int d) {
    const int rows = pat().rows;
    row_ = ((row_ + d) % rows + rows) % rows;
}

void PatternScreen::moveField(int d) {
    int f = field_ + d, ch = ui().channel;
    if (f == 2) f += d;   // the instrument is one field (1); 2 is not a stop
    const int fields = ch == kDrumCh ? 1 : kFields;
    if (f < 0) {
        ch = (ch + kTracks - 1) % kTracks;
        f = ch == kDrumCh ? 0 : kFields - 1;
    } else if (f >= fields) {
        f = 0;
        ch = (ch + 1) % kTracks;
    }
    field_ = f;
    ui().channel = ch;
}

void PatternScreen::setDrum(uint8_t slot) {
    pat().drum[row_] = slot;
    if (slot) ui().app().engine.drumPreview(slot);
    edited();
}

void PatternScreen::advance() {
    if (ui().step) moveRow(ui().step);
}

void PatternScreen::enterHex(int d) {
    Cell& c = cell();
    switch (field_) {
        case 1:
            c.inst = uint8_t(clampi((d << 4) | (c.inst & 15), 0, kInstrumentCount - 1));
            field_ = 2;
            break;
        case 2:
            c.inst = uint8_t(clampi((c.inst & 0xF0) | d, 0, kInstrumentCount - 1));
            if (c.inst) ui().inst = c.inst;
            field_ = 1;
            advance();
            break;
        case 3:
            c.cmd = uint8_t(d);
            field_ = 4;
            break;
        case 4:
            c.param = uint8_t((d << 4) | (c.param & 15));
            field_ = 5;
            break;
        case 5:
            c.param = uint8_t((c.param & 0xF0) | d);
            field_ = 4;
            advance();
            break;
        default: break;
    }
    edited();
}

void PatternScreen::encoder(int d) {
    if (!ui().edit) {
        moveRow(d);
        return;
    }
    if (ui().channel == kDrumCh) {
        setDrum(uint8_t(clampi(pat().drum[row_] + d, 0, audio::kKitSlots)));
        return;
    }
    Cell& c = cell();
    const bool sh = ui().shift;
    switch (field_) {
        case 0:
            if (note::isNote(c.note)) c.note = uint8_t(clampi(c.note + d * (sh ? 12 : 1), note::kFirst, note::kLast));
            else c.note = uint8_t(clampi(1 + ui().octave * 12, note::kFirst, note::kLast));
            if (!c.inst) c.inst = ui().inst;
            ui().app().engine.noteOn(ui().channel, c.note, c.inst);
            break;
        case 1:
        case 2:
            c.inst = uint8_t(clampi(c.inst + d * (sh ? 16 : 1), 0, kInstrumentCount - 1));
            if (c.inst) ui().inst = c.inst;
            break;
        case 3: c.cmd = uint8_t((c.cmd + d) & 15); break;
        default: c.param = uint8_t(c.param + d * (sh || field_ == 4 ? 16 : 1)); break;
    }
    edited();
}

bool PatternScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    if (!down) {
        ui().auditionOff(p);
        return true;
    }
    if (ui().channel == kDrumCh) {   // pads 1-16 = kit slots
        const uint8_t slot = uint8_t(p % 16 + 1);
        if (ui().edit) {
            setDrum(slot);
            advance();
        } else {
            ui().app().engine.drumPreview(slot);
        }
        return true;
    }
    if (field_ == 1 || field_ == 2) {
        // The instrument column, as on the stock Tracker: pad 1-48 is
        // instrument 1-48. In edit mode it is written into the step (an
        // empty step also gets a note) and the cursor moves on; otherwise
        // the pad just chooses it. Either way it plays and becomes current.
        if (p >= 48) return true;
        const uint8_t slot = uint8_t(p + 1);
        ui().inst = slot;
        ui().auditionNote(p, slot, uint8_t(1 + (ui().octave + 1) * 12));
        if (ui().edit) {
            writeInstrument(slot);
            advance();
        }
        return true;
    }
    if (field_ >= 3 && ui().edit && p < 16) {
        enterHex(p);
        return true;
    }
    const uint8_t n = ui().padNote(p);
    if (!n) return true;
    ui().auditionOn(p, ui().inst);
    if (ui().edit && field_ == 0) {
        Cell& c = cell();
        c.note = n;
        c.inst = ui().inst;
        advance();
        edited();
    }
    return true;
}

bool PatternScreen::hexPads() const { return ui().edit && field_ >= 3 && ui().channel != kDrumCh; }

bool PatternScreen::padColour(int p, uint32_t& rgb) {
    if ((field_ == 1 || field_ == 2) && ui().channel != kDrumCh) {
        // Instrument column: the pads are instruments 1-48; the step's one is lit.
        const uint8_t shown = cell().inst ? cell().inst : ui().inst;
        if (p + 1 == shown) {
            rgb = 0x00C060;
            return true;
        }
        rgb = p < 48 ? 0x081810 : 0;
        return true;
    }
    if (!hexPads() || p >= 16) return false;
    rgb = 0x701010;   // hex keys (UX plan P1.5)
    return true;
}

void PatternScreen::writeInstrument(uint8_t inst) {
    // The instrument picker / an instrument pad: "instruments are always
    // added with a note" (stock manual): an empty step also gets one.
    if (ui().channel == kDrumCh) return;
    Cell& c = cell();
    c.inst = inst;
    if (!c.note) c.note = uint8_t(1 + (ui().octave + 1) * 12);
    edited();
}

bool PatternScreen::midiNote(uint8_t n) {
    // In edit mode on a note field of a SID track: write it, as a pad would.
    if (!ui().edit || field_ != 0 || ui().channel == kDrumCh) return false;
    Cell& c = cell();
    c.note = n;
    c.inst = ui().inst;
    advance();
    edited();
    return true;
}

bool PatternScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    const bool sh = u.shift;
    Pattern& p = pat();
    switch (id) {
        case b::Up: moveRow(sh ? -16 : -1); return true;
        case b::Down: moveRow(sh ? 16 : 1); return true;
        case b::Left:
            if (sh) u.channel = (u.channel + kTracks - 1) % kTracks;
            else moveField(-1);
            if (u.channel == kDrumCh) field_ = 0;
            return true;
        case b::Right:
            if (sh) u.channel = (u.channel + 1) % kTracks;
            else moveField(1);
            if (u.channel == kDrumCh) field_ = 0;
            return true;
        case b::Note: field_ = 0; return true;
        case b::Instr:
            if (u.channel != kDrumCh) field_ = 1;
            return true;
        case b::Fx1:
            if (u.channel != kDrumCh) field_ = 3;
            return true;
        case b::Fx2:
            if (u.edit && u.channel != kDrumCh) {
                cell() = Cell{note::kKeyOff, 0, cell().cmd, cell().param};
                advance();
                edited();
            }
            return true;
        case b::Delete:
            if (!u.edit) return true;
            if (u.channel == kDrumCh) {
                if (sh) {
                    for (int r = row_; r + 1 < p.rows; ++r) p.drum[r] = p.drum[r + 1];
                    p.drum[p.rows - 1] = 0;
                } else {
                    p.drum[row_] = 0;
                    advance();
                }
                edited();
                return true;
            }
            if (sh) {
                for (int r = row_; r + 1 < p.rows; ++r) p.cells[r][u.channel] = p.cells[r + 1][u.channel];
                p.cells[p.rows - 1][u.channel] = Cell{};
            } else {
                Cell& c = cell();
                if (field_ == 0) c.note = c.inst = 0;
                else if (field_ <= 2) c = Cell{};   // the instrument column clears the step (stock)
                else c.cmd = c.param = 0;
                advance();
            }
            edited();
            return true;
        case b::Insert:
            if (!u.edit) return true;
            if (u.channel == kDrumCh) {
                for (int r = p.rows - 1; r > row_; --r) p.drum[r] = p.drum[r - 1];
                p.drum[row_] = 0;
                edited();
                return true;
            }
            for (int r = p.rows - 1; r > row_; --r) p.cells[r][u.channel] = p.cells[r - 1][u.channel];
            cell() = Cell{};
            edited();
            return true;
        case b::Copy:
            if (u.channel == kDrumCh) {
                if (sh) {
                    if (!clipDrumRows_) return true;
                    for (int r = 0; r < clipDrumRows_ && r < p.rows; ++r) p.drum[r] = clipDrum_[r];
                    edited();
                    u.status("Drums pasted");
                } else {
                    clipDrumRows_ = p.rows;
                    std::memcpy(clipDrum_, p.drum, sizeof clipDrum_);
                    u.status("Drums copied");
                }
                return true;
            }
            if (sh) {
                if (!clipRows_) return true;
                for (int r = 0; r < clipRows_ && r < p.rows; ++r) p.cells[r][u.channel] = clip_[r];
                edited();
                u.status("Track pasted");
            } else {
                clipRows_ = p.rows;
                for (int r = 0; r < p.rows; ++r) clip_[r] = p.cells[r][u.channel];
                u.status("Track copied");
            }
            return true;
        case b::EncPush:
            follow_ = !follow_;
            u.status(follow_ ? "Follow playback on" : "Follow playback off");
            return true;
        case b::F1:
        case b::F2:
        case b::F3:
        case b::F4:
        case b::F5:
        case b::F6:
        case b::F7:
            if (sh) {   // Shift: the mute page (tracks 1-6, F7 drums), as on the stock Tracker
                Engine& e = u.app().engine;
                const int ch = id - b::F1;
                e.setMuted(ch, !e.muted(ch));
                return true;
            }
            break;
        default: break;
    }
    switch (id) {
        case b::F1: u.pattern = uint8_t((u.pattern + kPatternCount - 1) % kPatternCount); return true;
        case b::F2: u.pattern = uint8_t((u.pattern + 1) % kPatternCount); return true;
        case b::F3: if (u.octave > 0) --u.octave; return true;
        case b::F4: if (u.octave < 6) ++u.octave; return true;
        case b::F5: if (u.inst > 1) --u.inst; return true;
        case b::F6: if (u.inst < kInstrumentCount - 1) ++u.inst; return true;
        case b::F7: u.step = uint8_t((u.step + 1) % 9); return true;
        case b::F8: {
            if (!u.edit) {
                u.status("Length: Rec first");
                return true;
            }
            int i = 0;
            while (i < int(sizeof kLengths) && kLengths[i] <= p.rows) ++i;
            p.rows = kLengths[i % int(sizeof kLengths)];
            if (row_ >= p.rows) row_ = p.rows - 1;
            edited();
            return true;
        }
        default: return false;
    }
}

void PatternScreen::labels(const char* out[8]) {
    static char step[12], len[12];
    std::snprintf(step, sizeof step, "STEP %u", unsigned(ui().step));
    std::snprintf(len, sizeof len, "LEN %u", unsigned(pat().rows));
    if (ui().shift) {   // the mute page
        static char m[7][10];
        const Engine& e = ui().app().engine;
        for (int i = 0; i < 7; ++i) {
            if (i < 6) std::snprintf(m[i], sizeof m[i], "%s %d", e.muted(i) ? "UNMUTE" : "MUTE", i + 1);
            else std::snprintf(m[i], sizeof m[i], "%s", e.muted(i) ? "UNMUTE DR" : "MUTE DR");
            out[i] = m[i];
        }
        out[7] = len;
        return;
    }
    out[0] = "PAT -";
    out[1] = "PAT +";
    out[2] = "OCT -";
    out[3] = "OCT +";
    out[4] = "INS -";
    out[5] = "INS +";
    out[6] = step;
    out[7] = len;
}

void PatternScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    Song& s = u.song();
    const Player& pl = u.app().engine.player();
    const bool here = pl.playing() && pl.patternIndex() == u.pattern;
    if (pl.playing() && follow_ && !u.edit) {
        u.pattern = pl.patternIndex();
        row_ = pl.row();
    }
    Pattern& p = pat();
    if (row_ >= p.rows) row_ = p.rows - 1;
    char buf[96];

    // Header: pattern, length, octave, instrument, step, edit state.
    std::snprintf(buf, sizeof buf, "PAT %02X  LEN %u  OCT %u  STEP %u", unsigned(u.pattern), unsigned(p.rows),
                  unsigned(u.octave), unsigned(u.step));
    g.text(8, kHeadY, kMono, col::kText, buf);
    std::snprintf(buf, sizeof buf, "INS %02X %s", unsigned(u.inst), s.inst[u.inst].name);
    g.text(340, kHeadY, kMono, col::kInst, buf);
    if (u.edit) g.text(740, kHeadY, kMono, col::kEdit, "EDIT");
    else if (!follow_) g.text(724, kHeadY, kMono, col::kDim, "NOFOLLOW");

    // SID1 / SID2 groups and channel headers.
    const int split = kGridX + 3 * kChanW - 6;
    g.text(kGridX, kChipY, kMono, col::kSid1, s.sid.model[0] == sid::Model::k6581 ? "SID1 6581" : "SID1 8580");
    g.text(kGridX + 3 * kChanW, kChipY, kMono, col::kSid2,
           s.sid.model[1] == sid::Model::k6581 ? "SID2 6581" : "SID2 8580");
    for (int ch = 0; ch < kChannels; ++ch) {
        const int x = kGridX + ch * kChanW;
        const bool m = u.app().engine.muted(ch), so = u.app().engine.solo(ch);
        std::snprintf(buf, sizeof buf, "CH%d %s", ch + 1, m ? "MUTE" : (so ? "SOLO" : ""));
        g.text(x, kChanY, kMono, m ? col::kDim : (ch < 3 ? col::kSid1 : col::kSid2), buf);
    }
    {
        const bool m = u.app().engine.muted(kDrumCh), so = u.app().engine.solo(kDrumCh);
        g.text(kDrumX - 4, kChanY, kMono, m ? col::kDim : col::kWarn, so ? "SO" : "DR");
    }
    g.vline(split, kChipY, kRowY + kRows * kRowH - kChipY, col::kFaint);
    g.vline(kDrumX - 7, kChipY, kRowY + kRows * kRowH - kChipY, col::kFaint);

    // Rows around the cursor: backgrounds, then one glyph layer per colour,
    // tracks outermost so the x >= 512 translate switches once per layer.
    // Editing: the cursor stays in the middle. Following playback: the view
    // stands still and turns a page when the play row reaches the bottom
    // (UX plan P2.3d), instead of scrolling the whole grid every row.
    const bool paging = pl.playing() && follow_ && !u.edit;
    const int maxTop = p.rows > kRows ? p.rows - kRows : 0;
    int top;
    if (paging) {
        if (row_ < viewTop_ || row_ >= viewTop_ + kRows) viewTop_ = row_;
        top = clampi(viewTop_, 0, maxTop);
    } else {
        top = clampi(row_ - kRows / 2, 0, maxTop);
        viewTop_ = top;
    }
    const int shown = p.rows - top < kRows ? p.rows - top : kRows;
    for (int i = 0; i < shown; ++i) {
        const int r = top + i;
        const int y = kRowY + i * kRowH;
        if (r == row_) g.fill(0, y, 800, kRowH, paging ? col::kPlayRow : col::kPanelHi);
        else if (here && r == pl.row()) g.fill(0, y, 800, kRowH, col::kPlayRow);
        else if (r % 4 == 0) g.fill(0, y, 800, kRowH, col::kRowHi);
    }
    enum Layer { kDimL, kRowNumL, kNoteL, kInstL, kCmdL, kDrumL, kLayers };
    const uint32_t layerCol[kLayers] = {col::kFaint, col::kText, col::kNote, col::kInst, col::kCmd, col::kWarn};
    for (int layer = 0; layer < kLayers; ++layer) {
        GlyphLayer gl(g, layerCol[layer]);
        if (layer == kDimL || layer == kRowNumL) {
            for (int i = 0; i < shown; ++i) {
                const int r = top + i;
                const bool bright = r % 4 == 0 || r == row_;   // the cursor / play row too (on its highlight)
                if (bright != (layer == kRowNumL)) continue;
                hex2(buf, uint8_t(r));
                buf[2] = 0;
                gl.put(2, kRowY + i * kRowH, buf);
            }
        }
        if (layer == kDimL || layer == kDrumL) {   // drum column
            for (int i = 0; i < shown; ++i) {
                const uint8_t d = p.drum[top + i];
                if ((d != 0) != (layer == kDrumL)) continue;
                hex2(buf, d);
                buf[2] = 0;
                gl.put(kDrumX, kRowY + i * kRowH, d ? buf : "--");
            }
            if (layer == kDrumL) continue;
        }
        for (int ch = 0; ch < kChannels; ++ch) {
            const int x = kGridX + ch * kChanW;
            for (int i = 0; i < shown; ++i) {
                const Cell& c = p.cells[top + i][ch];
                const int y = kRowY + i * kRowH;
                switch (layer) {
                    case kDimL:
                        if (!c.note) gl.put(x, y, "---");
                        if (!c.inst) gl.put(x + 32, y, "--");
                        if (!c.cmd && !c.param) gl.put(x + 56, y, "---");
                        break;
                    case kNoteL:
                        if (c.note) {
                            char n[4];
                            noteName(c.note, n);
                            gl.put(x, y, n);
                        }
                        break;
                    case kInstL:
                        if (c.inst) {
                            hex2(buf, c.inst);
                            buf[2] = 0;
                            gl.put(x + 32, y, buf);
                        }
                        break;
                    case kCmdL:
                        if (c.cmd || c.param) {
                            buf[0] = hexDigit(c.cmd);
                            hex2(buf + 1, c.param);
                            buf[3] = 0;
                            gl.put(x + 56, y, buf);
                        }
                        break;
                    default: break;
                }
            }
        }
    }
    // Cursor field box.
    {
        const int i = row_ - top;
        const bool drum = u.channel == kDrumCh;
        const int x = drum ? kDrumX : kGridX + u.channel * kChanW + kFieldChar[field_] * 8;
        const int w = drum ? 16 : kFieldW[field_] * 8;
        g.frame(x - 1, kRowY + i * kRowH, w + 2, kRowH, u.edit ? col::kEdit : col::kCursor, 2);
    }

    // Help line for the field under the cursor.
    const Cell& c = u.channel < kChannels ? p.cells[row_][u.channel] : p.cells[row_][0];
    if (u.channel == kDrumCh) {
        const uint8_t d = p.drum[row_];
        std::snprintf(buf, sizeof buf, "DRUMS %02X %s   pads: kit slots 1-16   jog: slot", unsigned(d),
                      d ? u.app().engine.kit().slot(d).name : "");
    } else if (field_ == 0) {
        std::snprintf(buf, sizeof buf, "NOTE  pads / jog: note   Fx2: key off ===   Shift+Delete: delete row");
    } else if (field_ <= 2) {
        std::snprintf(buf, sizeof buf, "INSTRUMENT %02X %.16s   pads 1-48: instrument   hold Instr: all", unsigned(c.inst),
                      c.inst ? s.inst[c.inst].name : "-");
    } else {
        std::snprintf(buf, sizeof buf, "CMD %X%02X  %s  (%s)", unsigned(c.cmd), unsigned(c.param), cmd::name(c.cmd),
                      cmd::paramHelp(c.cmd));
    }
    g.text(8, kHelpY, kMono, col::kDim, buf);
    if (hexPads()) g.text(596, kHelpY, kMono, col::kEdit, "HEX PADS");
    if (s.extendedFeatures()) g.text(680, kHelpY, kMono, col::kWarn, "EXT: no GT");
}

}  // namespace ui
}  // namespace sidt
