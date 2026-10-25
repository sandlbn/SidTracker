// SIDTracker -- Instrument screen (instrument_screen.h). Table semantics:
// docs/commands.md.
#include "ui/instrument/instrument_screen.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"
#include "tracker/song/tables.h"
#include "ui/settings/settings_screen.h"

namespace sidt {
namespace ui {

namespace {

constexpr int kInstNameCap = 64;

const uint16_t kAttackMs[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
const char* const kPageNames[InstrumentScreen::kPages] = {"GENERAL", "DESIGN", "WAVE", "PULSE", "FILTER", "TABLES",
                                                          "SOUNDS"};
const char* const kTableNames[kTableKinds] = {"WAVE", "PULSE", "FILTER", "SPEED"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// SOUNDS browsing (UX plan P1.3): the instrument and tables as they were
// before the first preset was tried, so CANCEL restores them exactly.
struct BrowseSnapshot {
    Instrument inst;
    Table tables[kTableKinds];
} gBrowse;

void waveName(uint8_t w, char* out, int cap) {
    int n = 0;
    out[0] = 0;
    const char* names[4] = {"TRI", "SAW", "PUL", "NOI"};
    for (int i = 0; i < 4; ++i)
        if (w & (0x10 << i)) n += std::snprintf(out + n, size_t(cap - n), "%s%s", n ? "+" : "", names[i]);
    if (!(w & 0xF0)) n += std::snprintf(out + n, size_t(cap - n), "NONE");
    if (w & 0x02) n += std::snprintf(out + n, size_t(cap - n), " sync");
    if (w & 0x04) n += std::snprintf(out + n, size_t(cap - n), " ring");
    if (w & 0x08) n += std::snprintf(out + n, size_t(cap - n), " test");
    if (w & 0x01) std::snprintf(out + n, size_t(cap - n), " gate");
}

void noteOffsetText(uint8_t r, char* out, int cap) {
    if (r <= 0x5F) std::snprintf(out, size_t(cap), "note +%d", int(r));
    else if (r <= 0x7F) std::snprintf(out, size_t(cap), "note %d", int(r) - 0x80);
    else if (r == 0x80) std::snprintf(out, size_t(cap), "note kept");
    else if (r <= 0xDF) {
        char n[4];
        noteName(uint8_t(r - 0x81 + 1), n);
        std::snprintf(out, size_t(cap), "note %s", n);
    } else std::snprintf(out, size_t(cap), "note kept");
}

}  // namespace

void describeTableRow(TableKind kind, uint8_t L, uint8_t R, char* out, int cap) {
    char a[40], b[24];
    if (L == 0xFF) {
        if (R) std::snprintf(out, size_t(cap), "FF: jump to row %02X", unsigned(R));
        else std::snprintf(out, size_t(cap), "FF 00: stop");
        return;
    }
    switch (kind) {
        case kWaveTable:
            noteOffsetText(R, b, sizeof b);
            if (L >= 0x10 && L <= 0xDF) {
                waveName(uint8_t(L & 0xFE), a, sizeof a);
                std::snprintf(out, size_t(cap), "wave %s, %s", a, b);
            } else if (L >= 0xE0 && L <= 0xEF) {
                std::snprintf(out, size_t(cap), "silent wave %X, %s", unsigned(L & 0x0E), b);
            } else if (L >= 0x01 && L <= 0x0F) {
                std::snprintf(out, size_t(cap), "%s, then wait %u ticks", b, unsigned(L));
            } else if (L == 0) {
                std::snprintf(out, size_t(cap), "wave kept, %s", b);
            } else {
                std::snprintf(out, size_t(cap), "reserved (no-op)");
            }
            return;
        case kPulseTable:
            if (L >= 0x80) std::snprintf(out, size_t(cap), "set pulse width %03X", unsigned(((L & 15) << 8) | R));
            else if (L) std::snprintf(out, size_t(cap), "%u ticks, width %+d per tick", unsigned(L), int(int8_t(R)));
            else std::snprintf(out, size_t(cap), "no-op");
            return;
        case kFilterTable:
            if (L >= 0x80) {
                std::snprintf(out, size_t(cap), "filter %s%s%s res %X route %c%c%c", (L & 0x10) ? "LP" : "",
                              (L & 0x20) ? "BP" : "", (L & 0x40) ? "HP" : "", unsigned(R >> 4), (R & 1) ? '1' : '-',
                              (R & 2) ? '2' : '-', (R & 4) ? '3' : '-');
            } else if (L) {
                std::snprintf(out, size_t(cap), "%u ticks, cutoff %+d per tick", unsigned(L), int(int8_t(R)));
            } else {
                std::snprintf(out, size_t(cap), "set cutoff %02X", unsigned(R));
            }
            return;
        default:
            std::snprintf(out, size_t(cap), "speed %04X | vibrato: %u ticks/half, depth %u",
                          unsigned((L << 8) | R), unsigned(L), unsigned(R));
            return;
    }
}

// --- TableView -----------------------------------------------------------------

void TableView::draw(ot::Gfx& g, const Song& s, int x, int y, int rows, bool focused, uint8_t pointer) {
    const Table& t = s.tables[kind];
    int top = row - rows / 2;
    top = clampi(top, 1, kTableLen - rows + 1);
    if (focused) g.fill(x, y + (row - top) * 16, 96, 16, col::kPanelHi);
    // Glyph layers (display-list budget, screen.h GlyphLayer): dim row
    // numbers and empty rows, the pointer row number, values, jumps.
    enum { kDimL, kPtrL, kValL, kJumpL, kLayers };
    const uint32_t colours[kLayers] = {col::kDim, col::kWarn, col::kText, col::kCmd};
    char buf[8];
    for (int layer = 0; layer < kLayers; ++layer) {
        GlyphLayer gl(g, colours[layer]);
        for (int i = 0; i < rows; ++i) {
            const int r = top + i;
            const int yy = y + i * 16;
            const bool isPtr = pointer && r == pointer;
            if ((layer == kDimL && !isPtr) || (layer == kPtrL && isPtr)) {
                hex2(buf, uint8_t(r));
                buf[2] = 0;
                gl.put(x + 2, yy, buf);
            }
            const bool empty = !t.l(r) && !t.r(r);
            const int want = empty ? kDimL : (t.l(r) == 0xFF ? kJumpL : kValL);
            if (layer != want) continue;
            hex2(buf, t.l(r));
            buf[2] = ' ';
            hex2(buf + 3, t.r(r));
            buf[5] = 0;
            gl.put(x + 32, yy, buf);
        }
    }
    if (focused) g.frame(x + 31 + col * 24, y + (row - top) * 16, 18, 16, col::kCursor, 2);
}

void TableView::edit(Song& s, int delta) {
    Table& t = s.tables[kind];
    uint8_t& v = col ? t.right[row - 1] : t.left[row - 1];
    v = uint8_t(v + delta);
}

void TableView::hex(Song& s, int d) {
    Table& t = s.tables[kind];
    uint8_t& v = col ? t.right[row - 1] : t.left[row - 1];
    if (nibble == 0) {
        v = uint8_t((d << 4) | (v & 15));
        nibble = 1;
    } else {
        v = uint8_t((v & 0xF0) | d);
        nibble = 0;
        if (col == 0) col = 1;
        else {
            col = 0;
            move(1);
        }
    }
}

void TableView::clearRow(Song& s) {
    s.tables[kind].left[row - 1] = 0;
    s.tables[kind].right[row - 1] = 0;
}

// --- InstrumentScreen -----------------------------------------------------------

Instrument& InstrumentScreen::inst() { return ui().song().inst[ui().inst]; }
void InstrumentScreen::edited() {
    ui().app().markDirty();
    usersKind_ = -1;   // the users line is recomputed
}

TableView& InstrumentScreen::view() {
    switch (page_) {
        case kWave: return views_[kWaveTable];
        case kPulse: return views_[kPulseTable];
        case kFilter: return views_[kFilterTable];
        default: return views_[tableFocus_];
    }
}

uint8_t* InstrumentScreen::pointerFor(TableKind k) {
    Instrument& in = inst();
    switch (k) {
        case kWaveTable: return &in.wavePtr;
        case kPulseTable: return &in.pulsePtr;
        case kFilterTable: return &in.filterPtr;
        default: return &in.vibParam;
    }
}

int InstrumentScreen::paramCount() const {
    switch (page_) {
        case kGeneral: return kGenRows;
        default: return 0;
    }
}

void InstrumentScreen::editParam(int d) {
    Instrument& in = inst();
    const int big = ui().shift ? 16 : 1;
    auto nib = [d](uint8_t& reg, bool high) {
        const int v = clampi((high ? reg >> 4 : reg & 15) + d, 0, 15);
        reg = uint8_t(high ? (reg & 0x0F) | (v << 4) : (reg & 0xF0) | v);
    };
    if (page_ == kGeneral) {
        switch (sel_) {
            case kGenName: editNameChar(in.name, nameCursor_, d); break;
            case kGenAttack: nib(in.ad, true); break;
            case kGenDecay: nib(in.ad, false); break;
            case kGenSustain: nib(in.sr, true); break;
            case kGenRelease: nib(in.sr, false); break;
            case kGenHr: in.gateTimer = uint8_t(in.gateTimer + d * big); break;
            case kGenWave: in.firstWave = uint8_t(in.firstWave + d * big); break;
            case kGenVib: in.vibParam = uint8_t(in.vibParam + d * big); break;
            case kGenVibDelay: in.vibDelay = uint8_t(in.vibDelay + d * big); break;
        }
    }
    edited();
    if (sel_ != kGenName) ui().retriggerHeld(ui().inst);   // hear the change on the held pads
}

void InstrumentScreen::encoder(int d) {
    if (page_ == kDesign) {
        designEncoder(d);
        return;
    }
    if (page_ == kSounds) {
        soundsEncoder(d);
        return;
    }
    if (page_ == kGeneral) {
        editParam(d);
        return;
    }
    TableView& v = view();
    if (ui().shift && page_ != kTables) {
        uint8_t* p = pointerFor(v.kind);
        *p = uint8_t(*p + d);
        if (*p) v.row = *p;
    } else {
        v.edit(ui().song(), d);
    }
    edited();
    ui().retriggerHeld(ui().inst);   // a held pad hears the table change
}

bool InstrumentScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    if (down && page_ >= kWave && page_ != kSounds && ui().edit && p < 16) {
        view().hex(ui().song(), p);
        edited();
        ui().retriggerHeld(ui().inst);
        return true;
    }
    return false;   // audition
}

bool InstrumentScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    if (u.shift && (id == b::Left || id == b::Right)) {   // the track the pads play on
        u.channel = (u.channel + (id == b::Left ? kChannels - 1 : 1)) % kChannels;
        designSlot_ = -1;
        return true;
    }
    if (browsing_ && browseSlot_ != u.inst) browsing_ = false;   // another slot was picked: its own try starts fresh
    if (browsing_ && !u.shift && (page_ == kSounds || page_ == kDesign) && (id == b::F7 || id == b::F8)) {
        if (id == b::F7) {   // CANCEL: back to the instrument as it was
            Song& s = u.song();
            s.inst[u.inst] = gBrowse.inst;
            std::memcpy(s.tables, gBrowse.tables, sizeof s.tables);
            edited();
            u.status("Sound cancelled");
        } else {
            u.status("Sound kept");
        }
        browsing_ = false;
        return true;
    }
    // Pages (UX plan P1.4): the Instr. Params key cycles them (Shift: back),
    // Shift+F1..F7 jumps to one; F1..F6 are the page's actions, F7 / F8 the
    // previous / next instrument.
    auto setPage = [&](int pg) {
        browsing_ = false;   // leaving SOUNDS keeps the preset being tried
        designSlot_ = -1;    // re-read the design (tables may have changed)
        page_ = Page(pg);
        sel_ = 0;
        if (page_ == kSounds && card()) {   // the card may have changed (a save, another folder)
            u.app().scanInstruments();
            if (preset_ >= categoryTotal()) preset_ = 0;
        }
    };
    if (id == b::InstrParams) {
        setPage((page_ + (u.shift ? kPages - 1 : 1)) % kPages);
        return true;
    }
    if (u.shift && id <= b::F7) {
        setPage(id - b::F1);
        return true;
    }
    if (id == b::F7 || id == b::F8) {
        browsing_ = false;
        u.inst = uint8_t(clampi(u.inst + (id == b::F7 ? -1 : 1), 1, kInstrumentCount - 1));
        nameCursor_ = 0;
        designSlot_ = -1;
        return true;
    }
    if (id <= b::F6) return pageAction(id - b::F1);
    if (page_ == kSounds) return soundsButton(id);
    if (page_ == kDesign) return designButton(id);
    if (page_ == kGeneral) {
        switch (id) {
            case b::Up: sel_ = (sel_ + paramCount() - 1) % paramCount(); return true;
            case b::Down: sel_ = (sel_ + 1) % paramCount(); return true;
            case b::Left:
                if (page_ == kGeneral && sel_ == 0 && nameCursor_ > 0) --nameCursor_;
                return true;
            case b::Right:
                if (page_ == kGeneral && sel_ == 0 && nameCursor_ < kNameLen - 1) ++nameCursor_;
                return true;
            case b::Insert:
            case b::EncPush:
                if (sel_ != kGenName) return true;
                if (const char* t = u.keyboardResult(id)) {   // OK on the pad keyboard
                    std::snprintf(inst().name, sizeof inst().name, "%s", t);
                    edited();
                } else {
                    u.keyboard(this, id, "INSTRUMENT NAME", inst().name);
                }
                return true;
            case b::Delete:
                // INIT: back to the init patch (the tables are left alone).
                {
                    char q[64];
                    std::snprintf(q, sizeof q, "Reset instrument %02X to the init patch?", unsigned(u.inst));
                    if (!u.confirm(this, b::Delete, q, "INIT")) return true;
                }
                u.song().inst[u.inst].clear(u.inst);
                designSlot_ = -1;
                nameCursor_ = 0;
                edited();
                u.status("Instrument initialised");
                return true;
            default: return false;
        }
    }
    TableView& v = view();
    switch (id) {
        case b::Up: v.move(u.shift ? -16 : -1); return true;
        case b::Down: v.move(u.shift ? 16 : 1); return true;
        case b::Left:
            if (page_ == kTables && v.col == 0) {
                tableFocus_ = (tableFocus_ + kTableKinds - 1) % kTableKinds;
                views_[tableFocus_].col = 1;
            } else {
                v.col = 0;
            }
            v.nibble = 0;
            return true;
        case b::Right:
            if (page_ == kTables && v.col == 1) {
                tableFocus_ = (tableFocus_ + 1) % kTableKinds;
                views_[tableFocus_].col = 0;
            } else {
                v.col = 1;
            }
            v.nibble = 0;
            return true;
        case b::EncPush:
        case b::Insert:
            *pointerFor(v.kind) = uint8_t(v.row);
            u.status("Instrument points here");
            edited();
            return true;
        case b::Delete:
            v.clearRow(u.song());
            edited();
            return true;
        default: return false;
    }
}

bool InstrumentScreen::padColour(int p, uint32_t& rgb) {
    if (!(ui().edit && page_ >= kWave && page_ != kSounds && p < 16)) return false;
    rgb = 0x701010;   // hex keys (UX plan P1.5)
    return true;
}

void InstrumentScreen::drawEnvelope(ot::Gfx& g, int x, int y, int w, int h) {
    const Instrument& in = inst();
    auto len = [](int rate, int mul) { return std::log10(1.0f + float(kAttackMs[rate] * mul)); };
    const float a = len(in.ad >> 4, 1), d = len(in.ad & 15, 3), r = len(in.sr & 15, 3);
    const float hold = 1.5f, total = a + d + hold + r;
    const float s = float(in.sr >> 4) / 15.0f;
    const int x1 = x + int(w * a / total), x2 = x1 + int(w * d / total), x3 = x2 + int(w * hold / total);
    const int ys = y + h - int(h * s);
    g.frame(x, y, w, h, col::kFaint);
    g.line(x, y + h, x1, y, col::kGood);
    g.line(x1, y, x2, ys, col::kGood);
    g.line(x2, ys, x3, ys, col::kGood);
    g.line(x3, ys, x + w, y + h, col::kGood);
}

void InstrumentScreen::drawParams(ot::Gfx& g) {
    const Instrument& in = inst();
    char v[40];
    const int x = 16, w = 420;
    int y = kContentY + 48;
    if (page_ == kGeneral) {
        listRow(g, x, y, w, "NAME", in.name, sel_ == kGenName);
        if (sel_ == kGenName)
            g.hline(x + w - 8 - (int(std::strlen(in.name)) - nameCursor_) * 8, y + 17, 8, col::kCursor);
        y += 24;
        const int a = in.ad >> 4, dc = in.ad & 15, su = in.sr >> 4, re = in.sr & 15;
        std::snprintf(v, sizeof v, "%X  %u ms", unsigned(a), unsigned(kAttackMs[a]));
        listRow(g, x, y, w, "ATTACK", v, sel_ == kGenAttack);
        y += 24;
        std::snprintf(v, sizeof v, "%X  %u ms", unsigned(dc), unsigned(kAttackMs[dc] * 3));
        listRow(g, x, y, w, "DECAY", v, sel_ == kGenDecay);
        y += 24;
        std::snprintf(v, sizeof v, "%X  %u %%", unsigned(su), unsigned(su * 100 / 15));
        listRow(g, x, y, w, "SUSTAIN", v, sel_ == kGenSustain);
        y += 24;
        std::snprintf(v, sizeof v, "%X  %u ms", unsigned(re), unsigned(kAttackMs[re] * 3));
        listRow(g, x, y, w, "RELEASE", v, sel_ == kGenRelease);
        y += 24;
        std::snprintf(v, sizeof v, "%02X  %u TICKS%s%s", unsigned(in.gateTimer), unsigned(in.gateTimer & 0x3F),
                      (in.gateTimer & 0x80) ? " NO-HR" : "", (in.gateTimer & 0x40) ? " LEGATO" : "");
        listRow(g, x, y, w, "HR / GATE TIMER", v, sel_ == kGenHr);
        y += 24;
        char wn[32];
        waveName(in.firstWave, wn, sizeof wn);
        std::snprintf(v, sizeof v, "%02X %s", unsigned(in.firstWave), in.firstWave ? wn : "(table)");
        listRow(g, x, y, w, "FIRST WAVE", v, sel_ == kGenWave);
        y += 24;
        std::snprintf(v, sizeof v, in.vibParam ? "SPEED ROW %02X" : "OFF", unsigned(in.vibParam));
        listRow(g, x, y, w, "VIBRATO", v, sel_ == kGenVib);
        y += 24;
        std::snprintf(v, sizeof v, in.vibDelay ? "%u TICKS" : "OFF (no vibrato)", unsigned(in.vibDelay));
        listRow(g, x, y, w, "VIBRATO DELAY", v, sel_ == kGenVibDelay);
        drawEnvelope(g, 470, kContentY + 72, 300, 120);
        std::snprintf(v, sizeof v, "ENVELOPE  pads play it on TRK %d", ui().channel % kChannels + 1);
        g.text(470, kContentY + 200, kMono, col::kDim, v);
        g.text(470, kContentY + 240, kMono, col::kDim, "Shift+Left/Right: other track");
        g.text(470, kContentY + 220, kMono, col::kDim, "Delete: INIT the instrument");
        y += 32;
        std::snprintf(v, sizeof v, "WAVE %02X  PULSE %02X  FILTER %02X", unsigned(in.wavePtr), unsigned(in.pulsePtr),
                      unsigned(in.filterPtr));
        g.text(x, y, kMono, col::kDim, "TABLE POINTERS");
        g.text(x + 140, y, kMono, col::kText, v);
    }
}

void InstrumentScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    const Song& s = u.song();
    char buf[96];
    std::snprintf(buf, sizeof buf, "INSTRUMENT %02X  %s", unsigned(u.inst), s.inst[u.inst].name);
    g.text(16, kContentY + 4, kLarge, col::kInst, buf);
    // Tab strip.
    // Tab strip, right-aligned; widths follow the names.
    int tx = 796;
    for (int i = kPages - 1; i >= 0; --i) {
        const int w = int(std::strlen(kPageNames[i])) * 8 + 10;
        tx -= w;
        if (i == page_) g.fill(tx, kContentY + 4, w - 2, 20, col::kPanelHi);
        g.text(tx + 4, kContentY + 6, kSmall, i == page_ ? col::kText : col::kDim, kPageNames[i]);
    }
    if (page_ == kGeneral) {
        drawParams(g);
        return;
    }
    if (page_ == kSounds) {
        drawSounds(g);
        return;
    }
    if (page_ == kDesign) {
        drawDesign(g);
        return;
    }
    if (page_ == kTables) {
        for (int k = 0; k < kTableKinds; ++k) {
            const int x = 16 + k * 196;
            std::snprintf(buf, sizeof buf, "%s %u/255", kTableNames[k], unsigned(s.tables[k].used()));
            g.text(x, kContentY + 40, kMono, k == tableFocus_ ? col::kText : col::kDim, buf);
            views_[k].kind = TableKind(k);
            views_[k].draw(g, s, x, kContentY + 60, 18, k == tableFocus_, *pointerFor(TableKind(k)));
        }
    } else {
        TableView& v = view();
        v.kind = page_ == kWave ? kWaveTable : (page_ == kPulse ? kPulseTable : kFilterTable);
        const uint8_t ptr = *pointerFor(v.kind);
        std::snprintf(buf, sizeof buf, "%s TABLE  pointer %02X  used %u/255", kTableNames[v.kind], unsigned(ptr),
                      unsigned(s.tables[v.kind].used()));
        g.text(16, kContentY + 40, kMono, col::kText, buf);
        v.draw(g, s, 16, kContentY + 60, 18, true, ptr);
        // Program listing from the pointer.
        g.text(140, kContentY + 60, kMono, col::kDim, "PROGRAM FROM POINTER");
        int row = ptr;
        for (int i = 0; i < 16 && row; ++i) {
            const Table& t = s.tables[v.kind];
            char d[64];
            describeTableRow(v.kind, t.l(row), t.r(row), d, sizeof d);
            std::snprintf(buf, sizeof buf, "%02X %s", unsigned(row), d);
            g.text(140, kContentY + 80 + i * 16, kMono, col::kText, buf);
            if (t.l(row) == 0xFF) break;
            row = row >= kTableLen ? 0 : row + 1;
        }
        if (page_ == kWave) {
            char wn[32];
            waveName(inst().firstWave, wn, sizeof wn);
            std::snprintf(buf, sizeof buf, "first wave %02X %s", unsigned(inst().firstWave), wn);
            g.text(560, kContentY + 40, kMono, col::kDim, buf);
        }
    }
    // Help line: the row under the cursor.
    TableView& v = view();
    const Table& t = s.tables[v.kind];
    char d[80];
    describeTableRow(v.kind, t.l(v.row), t.r(v.row), d, sizeof d);
    std::snprintf(buf, sizeof buf, "%s %02X: %s", kTableNames[v.kind], unsigned(v.row), d);
    g.text(16, kBarY - 22, kMono, col::kDim, buf);
    // Who uses this row (UX plan P2.7), recomputed when the cursor moves or
    // something is edited: the lookup scans every pattern.
    const int kind = page_ == kTables ? tableFocus_ : int(v.kind);
    if (usersKind_ != kind || usersRow_ != v.row) {
        usersKind_ = kind;
        usersRow_ = v.row;
        if (!tables::describeUsers(s, TableKind(kind), v.row, users_, sizeof users_))
            std::snprintf(users_, sizeof users_, "%s", t.l(v.row) || t.r(v.row) ? "not used by anything" : "");
    }
    g.text(16, kBarY - 40, kMono, col::kFaint, users_);
    if (u.edit) g.text(690, kBarY - 22, kMono, col::kEdit, "HEX PADS");   // pads 1-16 type hex
}

// --- SOUNDS page -----------------------------------------------------------------

void InstrumentScreen::browseLoad() {
    // Each preset tried replaces the previous try (the tables go back to the
    // snapshot first), so browsing never fills the tables.
    Ui& u = ui();
    Song& s = u.song();
    if (browsing_ && browseSlot_ != u.inst) browsing_ = false;   // never restore one slot's snapshot into another
    if (!browsing_) {
        browseSlot_ = u.inst;
        gBrowse.inst = s.inst[u.inst];
        std::memcpy(gBrowse.tables, s.tables, sizeof s.tables);
        browsing_ = true;
    } else {
        s.inst[u.inst] = gBrowse.inst;
        std::memcpy(s.tables, gBrowse.tables, sizeof s.tables);
    }
    if (!loadSelected()) {
        // The slot holds the instrument from before the try (the snapshot
        // was restored above): the try is over.
        s.inst[u.inst] = gBrowse.inst;
        std::memcpy(s.tables, gBrowse.tables, sizeof s.tables);
        browsing_ = false;
        u.message(card() ? "Cannot load that file (or table full)" : "Table full: preset not loaded", 2500);
        designSlot_ = -1;
        return;
    }
    designSlot_ = -1;
    edited();
    u.preview(u.inst);
}

int InstrumentScreen::categoryTotal() const {
    return card() ? ui().app().instrumentCount() : library::categoryCount(category_);
}

bool InstrumentScreen::loadSelected() {
    Ui& u = ui();
    if (card()) return u.app().loadInstrumentFile(preset_, u.inst);
    return library::load(u.song(), u.inst, library::preset(presetIndex()), u.channel % 3) == library::Result::kOk;
}

void InstrumentScreen::soundsEncoder(int d) {
    switch (sndRow_) {
        case 0: {
            const int n = library::kCategories + 1;   // + CARD
            category_ = ((category_ + d) % n + n) % n;
            preset_ = 0;
            if (card()) ui().app().scanInstruments();
            if (browsing_ && categoryTotal()) browseLoad();
            break;
        }
        case 1: {
            const int n = categoryTotal();
            if (!n) break;
            preset_ = ((preset_ + d) % n + n) % n;
            browseLoad();   // hear it at once; F7 CANCEL / F8 KEEP
            break;
        }
        case 2: arp_.chord = uint8_t(((arp_.chord + d) % library::kChords + library::kChords) % library::kChords); break;
        case 3: arp_.order = uint8_t(((arp_.order + d) % library::kOrders + library::kOrders) % library::kOrders); break;
        case 4: arp_.ticks = uint8_t(clampi(arp_.ticks + d, 1, 4)); break;
        case 5:
            arp_.wave = uint8_t(((arp_.wave + d) % library::kArpWaves + library::kArpWaves) % library::kArpWaves);
            break;
    }
}

bool InstrumentScreen::soundsButton(uint8_t id) {
    namespace b = ot::btn;
    Ui& u = ui();
    switch (id) {
        case b::Up: sndRow_ = (sndRow_ + 5) % 6; return true;
        case b::Down: sndRow_ = (sndRow_ + 1) % 6; return true;
        case b::EncPush:
        case b::Insert: {
            Song& s = u.song();
            if (sndRow_ <= 1 && browsing_) {   // the preset being tried: keep it
                browsing_ = false;
                u.status("Preset kept");
                return true;
            }
            if (sndRow_ <= 1) {
                if (!categoryTotal()) return true;   // an empty CARD: nothing to load
                if (!s.inst[u.inst].empty()) {
                    char q[64];
                    std::snprintf(q, sizeof q, "Replace %s with this sound?", s.inst[u.inst].name);
                    if (!u.confirm(this, id, q, "REPLACE")) return true;
                }
                if (!categoryTotal()) return true;
                if (loadSelected()) {
                    u.status("Preset loaded");
                    u.preview(u.inst);
                } else {
                    u.message(card() ? "Cannot load that file (or table full)" : "Table full: preset not loaded", 2500);
                }
            } else {
                const library::Result r = library::insertArp(s, u.inst, arp_);
                if (r == library::Result::kOk) {
                    u.status("Arpeggio written");
                    u.preview(u.inst);
                } else {
                    u.message("Wave table full", 2500);
                }
            }
            edited();
            return true;
        }
        default: return false;
    }
}

void InstrumentScreen::drawSounds(ot::Gfx& g) {
    Ui& u = ui();
    char v[64];
    const int x = 16, w = 380;
    int y = kContentY + 40;
    if (browsing_) g.text(x, y, kMono, col::kWarn, "TRYING: pads play it   CANCEL or KEEP below");
    else g.text(x, y, kMono, col::kDim, "SOUND LIBRARY (turn PRESET to try presets)");
    y += 20;
    std::snprintf(v, sizeof v, "%s  (%d)", card() ? "CARD" : library::categoryName(category_), categoryTotal());
    listRow(g, x, y, w, "CATEGORY", v, sndRow_ == 0);
    y += 24;
    if (card()) {
        App& app = u.app();
        if (!app.instrumentCount()) {
            listRow(g, x, y, w, "PRESET", "-", sndRow_ == 1);
            g.text(x, y + 24, kMono, col::kDim, "No .ins files in /SIDTracker/Instruments");
            g.text(x, y + 42, kMono, col::kDim, "GENERAL > SAVE puts one there");
        } else {
            char nm[kInstNameCap];
            std::snprintf(nm, sizeof nm, "%s", app.instrumentFile(preset_));
            if (char* dot = std::strrchr(nm, '.')) *dot = 0;
            std::snprintf(v, sizeof v, "%d/%d  %.30s", preset_ + 1, app.instrumentCount(), nm);
            listRow(g, x, y, w, "PRESET", v, sndRow_ == 1);
            g.text(x, y + 24, kMono, col::kText, "from the card: /SIDTracker/Instruments");
            const Instrument& in = u.song().inst[u.inst];
            std::snprintf(v, sizeof v, browsing_ ? "AD %02X SR %02X  (tables: WAVE / PULSE / FILTER pages)" : "turn PRESET to try it",
                          unsigned(in.ad), unsigned(in.sr));
            g.text(x, y + 42, kMono, col::kDim, v);
        }
        y += 78;
    } else {
        const library::Preset& p = library::preset(presetIndex());
        std::snprintf(v, sizeof v, "%d/%d  %s", preset_ + 1, library::categoryCount(category_), p.name);
        listRow(g, x, y, w, "PRESET", v, sndRow_ == 1);
        y += 24;
        g.text(x, y, kMono, col::kText, p.text);
        y += 18;
        std::snprintf(v, sizeof v, "AD %02X SR %02X  filter: voice %d (TRK %d, Shift+L/R)", unsigned(p.ad),
                      unsigned(p.sr), u.channel % 3 + 1, u.channel % kChannels + 1);
        g.text(x, y, kMono, col::kDim, v);
        y += 36;
    }
    g.text(x, y, kMono, col::kDim, "ARPEGGIATOR (writes the wave program)");
    y += 20;
    listRow(g, x, y, w, "CHORD", library::chordName(arp_.chord), sndRow_ == 2);
    y += 24;
    listRow(g, x, y, w, "ORDER", library::orderName(arp_.order), sndRow_ == 3);
    y += 24;
    std::snprintf(v, sizeof v, "%u TICK%s PER NOTE", unsigned(arp_.ticks), arp_.ticks > 1 ? "S" : "");
    listRow(g, x, y, w, "SPEED", v, sndRow_ == 4);
    y += 24;
    listRow(g, x, y, w, "WAVEFORM", library::arpWaveName(arp_.wave), sndRow_ == 5);
    y += 28;
    int8_t notes[10];
    const int n = library::arpNotes(arp_, notes, 10);
    int k = std::snprintf(v, sizeof v, "notes:");
    for (int i = 0; i < n && k < int(sizeof v) - 5; ++i) k += std::snprintf(v + k, sizeof v - size_t(k), " +%d", notes[i]);
    g.text(x, y, kMono, col::kDim, v);

    // Right: the preset's programs, as they will be written.
    const int rx = 420;
    int ry = kContentY + 40;
    g.text(rx, ry, kMono, col::kDim, sndRow_ <= 1 ? "PRESET PROGRAMS" : "WAVE PROGRAM (INSERT writes it)");
    ry += 20;
    if (sndRow_ >= 2) {
        library::Row rows[41];
        const int n = library::arpProgram(arp_, rows, 41);
        for (int i = 0; i < n && ry < kBarY - 24; ++i) {
            char d[56];
            describeTableRow(kWaveTable, rows[i].l, rows[i].r, d, sizeof d);
            std::snprintf(v, sizeof v, "%d %.44s", i + 1, d);
            g.text(rx, ry, kSmall, col::kText, v);
            ry += 14;
        }
        return;
    }
    if (card()) {
        g.text(rx, ry, kMono, col::kText, "A card file's programs are written");
        g.text(rx, ry + 18, kMono, col::kText, "when you try it: see them on the");
        g.text(rx, ry + 36, kMono, col::kText, "WAVE / PULSE / FILTER pages.");
        return;
    }
    const library::Preset& p = library::preset(presetIndex());
    library::Row filt[8];
    std::memcpy(filt, p.filter, sizeof filt);
    if (filt[0].l >= 0x80 && filt[0].l != 0xFF) filt[0].r = uint8_t((filt[0].r & 0xF0) | (1 << (u.channel % 3)));
    struct Prog {
        TableKind kind;
        const library::Row* rows;
        int cap;
    };
    const Prog progs[3] = {{kWaveTable, p.wave, 16}, {kPulseTable, p.pulse, 8}, {kFilterTable, filt, 8}};
    const char* names[3] = {"WAVE", "PULSE", "FILTER"};
    for (int k2 = 0; k2 < 3 && ry < kBarY - 40; ++k2) {
        const Prog& pr = progs[k2];
        if (!pr.rows[0].l && !pr.rows[0].r) continue;
        g.text(rx, ry, kMono, col::kInst, names[k2]);
        ry += 16;
        for (int i = 0; i < pr.cap && ry < kBarY - 24; ++i) {
            char d[56];
            describeTableRow(pr.kind, pr.rows[i].l, pr.rows[i].r, d, sizeof d);
            std::snprintf(v, sizeof v, "%d %.44s", i + 1, d);
            g.text(rx, ry, kSmall, col::kText, v);
            ry += 14;
            if (pr.rows[i].l == 0xFF) break;
        }
        ry += 4;
    }
}

// --- DESIGN page -------------------------------------------------------------------

namespace {
enum DRow {
    kDWave, kDTransient, kDAttack, kDDecay, kDSustain, kDRelease, kDPw, kDPwmSpeed, kDPwmTicks, kDHr,
    kDFilter, kDCutoff, kDRes, kDSweep, kDSweepSpeed, kDSweepTicks, kDVibDepth, kDVibSpeed, kDVibDelay, kDRows
};
constexpr int kDCol = kDFilter;   // first row of the right column
const char* const kDNames[kDRows] = {"WAVEFORM", "ATTACK HIT", "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
                                     "PULSE WIDTH", "PWM SPEED", "PWM RANGE", "HARD RESTART", "FILTER",
                                     "CUTOFF", "RESONANCE", "SWEEP", "SWEEP SPEED", "SWEEP LENGTH",
                                     "VIBRATO DEPTH", "VIBRATO SPEED", "VIBRATO DELAY"};
int wrap(int v, int n) { return ((v % n) + n) % n; }

// DESIGN pages (UX plan P3.1): columns under F1..F6.
constexpr int kDSubs = 4;
const char* const kDSubNames[kDSubs] = {"OSC / ENVELOPE", "PULSE", "FILTER", "VIBRATO"};
const int kDSubCols[kDSubs] = {6, 4, 6, 3};
const int kDSub[kDSubs][6] = {{kDWave, kDTransient, kDAttack, kDDecay, kDSustain, kDRelease},
                              {kDPw, kDPwmSpeed, kDPwmTicks, kDHr, -1, -1},
                              {kDFilter, kDCutoff, kDRes, kDSweep, kDSweepSpeed, kDSweepTicks},
                              {kDVibDepth, kDVibSpeed, kDVibDelay, -1, -1, -1}};
const char* const kDShort[kDRows] = {"WAVE", "HIT", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "WIDTH", "PWM SPEED",
                                     "PWM RANGE", "HARD RST", "FILTER", "CUTOFF", "RESONANCE", "SWEEP", "SW SPEED",
                                     "SW LENGTH", "DEPTH", "SPEED", "DELAY"};
void designPos(int row, int& sub, int& col) {
    for (int s2 = 0; s2 < kDSubs; ++s2)
        for (int c = 0; c < kDSubCols[s2]; ++c)
            if (kDSub[s2][c] == row) {
                sub = s2;
                col = c;
                return;
            }
    sub = col = 0;
}
}  // namespace

designer::Design& InstrumentScreen::design() {
    if (designSlot_ != ui().inst) {
        design_ = designer::decode(ui().song(), ui().inst);
        designSlot_ = ui().inst;
    }
    return design_;
}

void InstrumentScreen::designEncoder(int d) {
    designer::Design& x = design();
    const int big = ui().shift ? 16 : 1;
    auto add8 = [&](uint8_t& v, int lo, int hi) { v = uint8_t(clampi(int(v) + d * big, lo, hi)); };
    switch (dRow_) {
        case kDWave: {
            const uint8_t* w;
            const int n = designer::waveChoices(&w);
            int k = 0;
            while (k < n && w[k] != x.wave) ++k;
            x.wave = w[wrap(k + d, n)];
            break;
        }
        case kDTransient: x.transient = uint8_t(wrap(x.transient + d, designer::kTransients)); break;
        case kDAttack: x.attack = uint8_t(clampi(x.attack + d, 0, 15)); break;
        case kDDecay: x.decay = uint8_t(clampi(x.decay + d, 0, 15)); break;
        case kDSustain: x.sustain = uint8_t(clampi(x.sustain + d, 0, 15)); break;
        case kDRelease: x.release = uint8_t(clampi(x.release + d, 0, 15)); break;
        case kDPw: x.pw = uint16_t(clampi(x.pw + d * (ui().shift ? 0x200 : 0x40), 0, 0xFFF)); break;
        case kDPwmSpeed: add8(x.pwmSpeed, 0, 0x7F); break;
        case kDPwmTicks: add8(x.pwmTicks, 1, 0x7F); break;
        case kDHr: x.hardRestart = !x.hardRestart; break;
        case kDFilter: x.filter = uint8_t(wrap(x.filter + d, designer::kFilterTypes)); break;
        case kDCutoff: add8(x.cutoff, 0, 255); break;
        case kDRes: x.resonance = uint8_t(clampi(x.resonance + d, 0, 15)); break;
        case kDSweep: x.sweep = uint8_t(wrap(x.sweep + d, designer::kSweeps)); break;
        case kDSweepSpeed: add8(x.sweepSpeed, 1, 0x7F); break;
        case kDSweepTicks: add8(x.sweepTicks, 1, 0x7F); break;
        case kDVibDepth: add8(x.vibDepth, 0, 255); break;
        case kDVibSpeed: add8(x.vibSpeed, 1, 0x7F); break;
        case kDVibDelay: add8(x.vibDelay, 1, 255); break;
    }
    namespace dz = designer;
    uint8_t section = dz::kWaveProg;
    switch (dRow_) {
        case kDAttack: case kDDecay: case kDSustain: case kDRelease: section = dz::kEnv; break;
        case kDHr: section = dz::kHr; break;
        case kDPw: case kDPwmSpeed: case kDPwmTicks: section = dz::kPulseProg; break;
        case kDFilter: case kDCutoff: case kDRes: case kDSweep: case kDSweepSpeed: case kDSweepTicks:
            section = dz::kFilterProg;
            break;
        case kDVibDepth: case kDVibSpeed: case kDVibDelay: section = dz::kVib; break;
        default: break;
    }
    // A sound the designer did not make: its first wave / pulse change
    // replaces both programs, so the result is a complete designer sound.
    if ((section & (dz::kWaveProg | dz::kPulseProg)) && !dz::isDesigned(ui().song(), ui().inst))
        section = uint8_t(section | dz::kWaveProg | dz::kPulseProg | dz::kEnv);
    if (designer::apply(ui().song(), ui().inst, x, ui().channel % 3, section) != designer::Result::kOk) {
        ui().message("Table full: change not written", 2500);
        designSlot_ = -1;
        return;
    }
    edited();
    ui().retriggerHeld(ui().inst);
}

bool InstrumentScreen::designButton(uint8_t id) {
    // Four pages of up to six columns (UX plan P3.1): Up / Down = page,
    // Left / Right (or F1-F6) = column, jog = value.
    namespace b = ot::btn;
    int sub = 0, col = 0;
    designPos(dRow_, sub, col);
    switch (id) {
        case b::Up:
        case b::Down:
            sub = wrap(sub + (id == b::Up ? -1 : 1), kDSubs);
            dRow_ = kDSub[sub][clampi(col, 0, kDSubCols[sub] - 1)];
            return true;
        case b::Left:
        case b::Right:
            col = wrap(col + (id == b::Left ? -1 : 1), kDSubCols[sub]);
            dRow_ = kDSub[sub][col];
            return true;
        case b::Insert:   // a variation (Shift: a new random sound); F7 CANCEL / F8 KEEP
            randomise(ui().shift);
            return true;
        default: return false;
    }
}

void InstrumentScreen::randomise(bool fresh) {
    // UX plan P3.3: tried like a preset, so CANCEL restores the instrument.
    Ui& u = ui();
    Song& s = u.song();
    if (browsing_ && browseSlot_ != u.inst) browsing_ = false;
    if (!browsing_) {
        browseSlot_ = u.inst;
        gBrowse.inst = s.inst[u.inst];
        std::memcpy(gBrowse.tables, s.tables, sizeof s.tables);
        browsing_ = true;
        tryBase_ = designer::decode(s, u.inst);   // variations stay near the sound you had
    } else {
        s.inst[u.inst] = gBrowse.inst;
        std::memcpy(s.tables, gBrowse.tables, sizeof s.tables);
    }
    static uint32_t seed = 0x9E3779B9u;
    seed ^= u.now() * 2654435761u + 1;
    auto rnd = [](int n) {   // xorshift
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return n > 0 ? int(seed % uint32_t(n)) : 0;
    };
    auto near = [&](int v, int spread, int lo, int hi) { return clampi(v + rnd(2 * spread + 1) - spread, lo, hi); };
    designer::Design d = tryBase_;
    const uint8_t* waves;
    const int nw = designer::waveChoices(&waves);
    if (fresh) {
        d.wave = waves[rnd(nw)];
        d.transient = uint8_t(rnd(designer::kTransients));
        d.attack = uint8_t(rnd(6));
        d.decay = uint8_t(4 + rnd(9));
        d.sustain = uint8_t(rnd(16));
        d.release = uint8_t(3 + rnd(10));
        d.pw = uint16_t(0x100 + rnd(0xD00));
        d.pwmSpeed = uint8_t(rnd(3) ? rnd(0x40) : 0);
        d.pwmTicks = uint8_t(8 + rnd(0x60));
        d.filter = uint8_t(rnd(2) ? 1 + rnd(designer::kFilterTypes - 1) : designer::kFilterOff);
        d.cutoff = uint8_t(0x20 + rnd(0xC0));
        d.resonance = uint8_t(rnd(16));
        d.sweep = uint8_t(rnd(designer::kSweeps));
        d.sweepSpeed = uint8_t(1 + rnd(8));
        d.sweepTicks = uint8_t(8 + rnd(0x60));
        d.vibDepth = uint8_t(rnd(3) ? 0 : 1 + rnd(0x30));
        d.vibSpeed = uint8_t(2 + rnd(6));
        d.vibDelay = uint8_t(4 + rnd(30));
        d.hardRestart = true;
    } else {
        // Nudge most controls a little; the waveform changes now and then.
        if (!rnd(5)) d.wave = waves[rnd(nw)];
        d.attack = uint8_t(near(d.attack, 2, 0, 15));
        d.decay = uint8_t(near(d.decay, 2, 0, 15));
        d.sustain = uint8_t(near(d.sustain, 3, 0, 15));
        d.release = uint8_t(near(d.release, 2, 0, 15));
        d.pw = uint16_t(near(d.pw, 0x200, 0x080, 0xF80));
        d.pwmSpeed = uint8_t(near(d.pwmSpeed, 8, 0, 0x7F));
        d.pwmTicks = uint8_t(near(d.pwmTicks, 12, 1, 0x7F));
        d.cutoff = uint8_t(near(d.cutoff, 0x20, 0, 0xFF));
        d.resonance = uint8_t(near(d.resonance, 3, 0, 15));
        d.sweepSpeed = uint8_t(near(d.sweepSpeed, 2, 1, 0x7F));
        if (d.vibDepth) d.vibDepth = uint8_t(near(d.vibDepth, 6, 1, 0xFF));
    }
    if (designer::apply(s, u.inst, d, u.channel % 3) != designer::Result::kOk) {
        s.inst[u.inst] = gBrowse.inst;
        std::memcpy(s.tables, gBrowse.tables, sizeof s.tables);
        browsing_ = false;
        u.message("Table full: no room for a new sound", 2500);
        return;
    }
    designSlot_ = -1;
    edited();
    u.status(fresh ? "Random sound: CANCEL or KEEP" : "Variation: CANCEL or KEEP");
    u.preview(u.inst);
}

void InstrumentScreen::drawDesign(ot::Gfx& g) {
    const designer::Design& x = design();
    char v[40];
    auto value = [&](int r) {
        switch (r) {
            case kDWave: std::snprintf(v, sizeof v, "%s", designer::waveName(x.wave)); break;
            case kDTransient: std::snprintf(v, sizeof v, "%s", designer::transientName(x.transient)); break;
            case kDAttack: std::snprintf(v, sizeof v, "%X  %u ms", unsigned(x.attack), unsigned(kAttackMs[x.attack])); break;
            case kDDecay: std::snprintf(v, sizeof v, "%X  %u ms", unsigned(x.decay), unsigned(kAttackMs[x.decay] * 3)); break;
            case kDSustain: std::snprintf(v, sizeof v, "%X", unsigned(x.sustain)); break;
            case kDRelease: std::snprintf(v, sizeof v, "%X  %u ms", unsigned(x.release), unsigned(kAttackMs[x.release] * 3)); break;
            case kDPw: std::snprintf(v, sizeof v, "%03X  %u%%", unsigned(x.pw), unsigned(x.pw * 100 / 4096)); break;
            case kDPwmSpeed: std::snprintf(v, sizeof v, x.pwmSpeed ? "%u" : "OFF", unsigned(x.pwmSpeed)); break;
            case kDPwmTicks: std::snprintf(v, sizeof v, "%u TICKS", unsigned(x.pwmTicks)); break;
            case kDHr: std::snprintf(v, sizeof v, "%s", x.hardRestart ? "ON" : "OFF"); break;
            case kDFilter: std::snprintf(v, sizeof v, "%s", designer::filterName(x.filter)); break;
            case kDCutoff: std::snprintf(v, sizeof v, "%02X", unsigned(x.cutoff)); break;
            case kDRes: std::snprintf(v, sizeof v, "%X", unsigned(x.resonance)); break;
            case kDSweep: std::snprintf(v, sizeof v, "%s", designer::sweepName(x.sweep)); break;
            case kDSweepSpeed: std::snprintf(v, sizeof v, "%u", unsigned(x.sweepSpeed)); break;
            case kDSweepTicks: std::snprintf(v, sizeof v, "%u TICKS", unsigned(x.sweepTicks)); break;
            case kDVibDepth: std::snprintf(v, sizeof v, x.vibDepth ? "%02X" : "OFF", unsigned(x.vibDepth)); break;
            case kDVibSpeed: std::snprintf(v, sizeof v, "%u", unsigned(x.vibSpeed)); break;
            default: std::snprintf(v, sizeof v, "%u TICKS", unsigned(x.vibDelay)); break;
        }
    };
    const bool noFilter = x.filter == designer::kFilterOff;
    auto frac = [&](int r) -> float {
        switch (r) {
            case kDWave: {
                const uint8_t* w;
                const int n = designer::waveChoices(&w);
                int k = 0;
                while (k < n && w[k] != x.wave) ++k;
                return n > 1 ? float(k) / float(n - 1) : 0.0f;
            }
            case kDTransient: return float(x.transient) / float(designer::kTransients - 1);
            case kDAttack: return float(x.attack) / 15.0f;
            case kDDecay: return float(x.decay) / 15.0f;
            case kDSustain: return float(x.sustain) / 15.0f;
            case kDRelease: return float(x.release) / 15.0f;
            case kDPw: return float(x.pw) / 4095.0f;
            case kDPwmSpeed: return float(x.pwmSpeed) / 127.0f;
            case kDPwmTicks: return float(x.pwmTicks) / 127.0f;
            case kDHr: return x.hardRestart ? 1.0f : 0.0f;
            case kDFilter: return float(x.filter) / float(designer::kFilterTypes - 1);
            case kDCutoff: return float(x.cutoff) / 255.0f;
            case kDRes: return float(x.resonance) / 15.0f;
            case kDSweep: return float(x.sweep) / float(designer::kSweeps - 1);
            case kDSweepSpeed: return float(x.sweepSpeed) / 127.0f;
            case kDSweepTicks: return float(x.sweepTicks) / 127.0f;
            case kDVibDepth: return float(x.vibDepth) / 255.0f;
            case kDVibSpeed: return float(x.vibSpeed) / 127.0f;
            default: return float(x.vibDelay) / 255.0f;
        }
    };
    int sub = 0, cur = 0;
    designPos(dRow_, sub, cur);
    // Page tabs (Up / Down).
    for (int i = 0, tx = 16; i < kDSubs; ++i) {
        const int w = int(std::strlen(kDSubNames[i])) * 8 + 16;
        if (i == sub) g.fill(tx - 4, kContentY + 32, w, 20, col::kPanelHi);
        g.text(tx + 4, kContentY + 34, kMono, i == sub ? col::kText : col::kDim, kDSubNames[i]);
        tx += w + 6;
    }
    g.text(600, kContentY + 34, kMono, col::kFaint, "Up/Down: page");
    // Columns under F1..F6: name, bar, value.
    const int top = kContentY + 60, bh = 150;
    for (int c = 0; c < kDSubCols[sub]; ++c) {
        const int r = kDSub[sub][c];
        const int bx = c * 100 + 3;
        const bool sel = r == dRow_;
        const bool off = (noFilter && r > kDFilter && r <= kDSweepTicks) ||
                         (x.sweep == designer::kSweepOff && (r == kDSweepSpeed || r == kDSweepTicks)) ||
                         (!x.vibDepth && (r == kDVibSpeed || r == kDVibDelay)) ||
                         (!(x.wave & 0x40) && (r == kDPw || r == kDPwmSpeed || r == kDPwmTicks));
        g.fill(bx, top, 94, bh + 48, sel ? col::kPanelHi : col::kPanel);
        g.text(bx + 47, top + 4, kSmall, off ? col::kDim : col::kText, kDShort[r], ot::IDisplay::kOptCenter);
        g.fill(bx + 35, top + 22, 24, bh - 30, col::kFaint);
        float f = frac(r);
        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        const int h = int(f * float(bh - 30));
        if (h > 0) g.fill(bx + 35, top + 22 + bh - 30 - h, 24, h, off ? col::kDim : (sel ? col::kCursor : col::kGood));
        value(r);
        g.text(bx + 47, top + bh + 4, kSmall, off ? col::kDim : col::kText, v, ot::IDisplay::kOptCenter);
    }
    // Graphs: envelope, pulse shape, filter response.
    const int gy = top + bh + 56;
    drawEnvelope(g, 16, gy, 180, 70);
    {
        const int px = 210, pw = 170, ph = 70;
        g.frame(px, gy, pw, ph, col::kFaint);
        if (x.wave & 0x40) {
            const int hi = 6 + int(float(pw / 2 - 12) * float(x.pw) / 4096.0f);
            for (int k = 0; k < 2; ++k) {
                const int x0 = px + 6 + k * (pw / 2 - 6);
                g.line(x0, gy + ph - 10, x0, gy + 10, col::kGood);
                g.line(x0, gy + 10, x0 + hi, gy + 10, col::kGood);
                g.line(x0 + hi, gy + 10, x0 + hi, gy + ph - 10, col::kGood);
                g.line(x0 + hi, gy + ph - 10, x0 + pw / 2 - 6, gy + ph - 10, col::kGood);
            }
        } else {
            g.text(px + pw / 2, gy + ph / 2 - 8, kSmall, col::kDim, "no pulse", ot::IDisplay::kOptCenter);
        }
    }
    {
        const int fx = 410, fw = 374, fh = 70;
        g.frame(fx, gy, fw, fh, col::kFaint);
        if (noFilter) {
            g.text(fx + fw / 2, gy + fh / 2 - 8, kSmall, col::kDim, "filter off", ot::IDisplay::kOptCenter);
        } else {
            // Rough response: cutoff position, slope 12 dB/oct, resonance peak.
            const float c = float(x.cutoff) / 255.0f, q = float(x.resonance) / 15.0f;
            int px = fx, py = 0;
            for (int i = 0; i <= 40; ++i) {
                const float f = float(i) / 40.0f, dist = (f - c) * 6.0f;
                float lp = dist <= 0.0f ? 1.0f : 1.0f / (1.0f + dist * dist * 4.0f);
                float hp = dist >= 0.0f ? 1.0f : 1.0f / (1.0f + dist * dist * 4.0f);
                const float peak = q * 0.6f / (1.0f + dist * dist * 40.0f);
                float y;
                switch (x.filter) {
                    case designer::kHighPass: y = hp + peak; break;
                    case designer::kBandPass: y = lp * hp * 1.0f / (1.0f + dist * dist) + peak; break;
                    case designer::kNotch: y = 1.0f - 0.9f / (1.0f + dist * dist * 8.0f); break;
                    default: y = lp + peak; break;
                }
                if (y > 1.4f) y = 1.4f;
                const int xx = fx + int(f * float(fw - 1)), yy = gy + fh - 4 - int(y / 1.4f * float(fh - 8));
                if (i) g.line(px, py, xx, yy, col::kCmd);
                px = xx;
                py = yy;
            }
        }
    }
    const char* note = designer::isDesigned(ui().song(), ui().inst)
                           ? "Hold a pad and turn: every change replays the note."
                           : "Envelope / filter: only that part. Wave / pulse: makes a designer sound.";
    if (browsing_) note = "TRYING: CANCEL or KEEP below   Insert: another variation  Shift+Insert: random";
    else if (designer::isDesigned(ui().song(), ui().inst)) note = "Insert: a variation   Shift+Insert: a random sound   (hold a pad: hear it)";
    g.text(16, kBarY - 22, kMono, col::kDim, note);
}

bool InstrumentScreen::pageAction(int k) {
    Ui& u = ui();
    Song& s = u.song();
    if (page_ == kGeneral) {
        static Instrument clip;
        static bool haveClip = false;
        switch (k) {
            case 0: return button(ot::btn::Delete, true);   // INIT (asks)
            case 1:
                clip = inst();
                haveClip = true;
                u.status("Instrument copied");
                return true;
            case 2: {
                if (!haveClip) {
                    u.status("Nothing copied");
                    return true;
                }
                if (!inst().empty()) {
                    char q[64];
                    std::snprintf(q, sizeof q, "Paste over %s?", inst().name);
                    if (!u.confirm(this, ot::btn::F3, q, "PASTE")) return true;
                }
                inst() = clip;   // shares the copied instrument's table programs
                designSlot_ = -1;
                edited();
                u.status("Instrument pasted (same table programs)");
                return true;
            }
            case 3: {   // SAVE to the card library (/SIDTracker/Instruments/User)
                App& app = u.app();
                if (!app.platform().storage) {
                    u.message("No SD card");
                    return true;
                }
                if (const char* t = u.keyboardResult(ot::btn::F4)) {
                    std::snprintf(saveName_, sizeof saveName_, "%s", t);
                } else if (!u.confirming(ot::btn::F4)) {
                    char name[kNameLen + 1];
                    int n = 0;
                    for (const char* c = inst().name; *c && n < kNameLen; ++c) {
                        const char ch = char(std::toupper(static_cast<unsigned char>(*c)));
                        name[n++] = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ? ch : '_';
                    }
                    name[n] = 0;
                    u.keyboard(this, ot::btn::F4, "SAVE INSTRUMENT AS (Instruments/User)", n ? name : "INSTR", true);
                    return true;
                }
                char path[128];
                std::snprintf(path, sizeof path, "%s/User/%s.ins", App::kInstDir, saveName_);
                if (app.platform().storage->exists(path)) {
                    char q[64];
                    std::snprintf(q, sizeof q, "%s exists. Overwrite it?", saveName_);
                    if (!u.confirm(this, ot::btn::F4, q, "OVERWRITE")) return true;
                }
                if (app.saveInstrumentFile(u.inst, saveName_)) {
                    app.scanInstruments();   // SOUNDS > CARD lists it at once
                    char m[64];
                    std::snprintf(m, sizeof m, "Saved to Instruments/User/%s.ins", saveName_);
                    u.message(m, 2500);
                } else {
                    u.message("Save failed", 2500);
                }
                return true;
            }
            default: return true;
        }
    }
    if (page_ == kDesign) {   // F1..F6: a column of the current design page
        int sub = 0, col = 0;
        designPos(dRow_, sub, col);
        if (k < kDSubCols[sub]) dRow_ = kDSub[sub][k];
        return true;
    }
    if (page_ == kSounds) return true;
    TableView& v = view();
    const TableKind kind = page_ == kTables ? TableKind(tableFocus_) : v.kind;
    switch (k) {
        case 0:
            if (!tables::insertRow(s, kind, v.row)) u.message("Table full: the last row is in use", 2500);
            else edited();
            return true;
        case 1:
            tables::deleteRow(s, kind, v.row);
            edited();
            return true;
        case 2:
            *pointerFor(kind) = uint8_t(v.row);
            u.status("Instrument points here");
            edited();
            return true;
        case 3: {
            char buf[64];
            const int n = tables::describeUsers(s, kind, v.row, buf, sizeof buf);
            if (!n) u.message("Row not used by anything", 2500);
            else u.message(buf, 3500);
            return true;
        }
        case 4: {
            if (!u.confirm(this, ot::btn::F5, "Remove the rows nothing uses? (Shift+Rec undoes)", "PACK")) return true;
            const int n = tables::pack(s, kind);
            char m[40];
            std::snprintf(m, sizeof m, "%d unused rows removed", n);
            u.status(m);
            if (n) edited();
            return true;
        }
        default: return true;
    }
}

void InstrumentScreen::labels(const char* out[8]) {
    if (ui().shift) {   // Shift+F1..F7: go to a page
        for (int i = 0; i < kPages; ++i) out[i] = kPageNames[i];
        return;
    }
    static const char* const kGen[6] = {"INIT", "COPY", "PASTE", "SAVE", nullptr, nullptr};
    static const char* kDes[6];
    if (page_ == kDesign) {
        int sub = 0, col = 0;
        designPos(dRow_, sub, col);
        for (int i = 0; i < 6; ++i) kDes[i] = i < kDSubCols[sub] ? kDShort[kDSub[sub][i]] : nullptr;
    }
    static const char* const kTab[6] = {"INS ROW", "DEL ROW", "POINT HERE", "USERS", "PACK", nullptr};
    const char* const* a = page_ == kGeneral ? kGen : (page_ == kDesign ? kDes : (page_ == kSounds ? nullptr : kTab));
    for (int i = 0; i < 6; ++i) out[i] = a ? a[i] : nullptr;
    out[6] = "PREV INS";
    out[7] = "NEXT INS";
    if (browsing_ && (page_ == kSounds || page_ == kDesign)) {
        out[6] = "CANCEL";
        out[7] = "KEEP";
    }
}


}  // namespace ui
}  // namespace sidt
