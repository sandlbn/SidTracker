// SIDTracker -- Instrument screen (instrument_screen.h). Table semantics:
// docs/commands.md.
#include "ui/instrument/instrument_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"
#include "ui/settings/settings_screen.h"

namespace sidt {
namespace ui {

namespace {

const uint16_t kAttackMs[16] = {2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000};
const char* const kPageNames[InstrumentScreen::kPages] = {"GENERAL", "DESIGN", "WAVE", "PULSE", "FILTER", "TABLES",
                                                          "SOUNDS"};
const char* const kTableNames[kTableKinds] = {"WAVE", "PULSE", "FILTER", "SPEED"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
void InstrumentScreen::edited() { ui().app().markDirty(); }

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
        case kGeneral: return 5;
        default: return 0;
    }
}

void InstrumentScreen::editParam(int d) {
    Instrument& in = inst();
    const int big = ui().shift ? 16 : 1;
    if (page_ == kGeneral) {
        switch (sel_) {
            case 0: editNameChar(in.name, nameCursor_, d); break;
            case 1: in.gateTimer = uint8_t(in.gateTimer + d * big); break;
            case 2: in.firstWave = uint8_t(in.firstWave + d * big); break;
            case 3: in.vibParam = uint8_t(in.vibParam + d * big); break;
            case 4: in.vibDelay = uint8_t(in.vibDelay + d * big); break;
        }
    }
    edited();
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
}

bool InstrumentScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    if (down && page_ >= kWave && page_ != kSounds && ui().edit && p < 16) {
        view().hex(ui().song(), p);
        edited();
        return true;
    }
    return false;   // audition
}

bool InstrumentScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    if (id <= b::F7) {
        designSlot_ = -1;   // re-read the design (tables may have changed)
        page_ = Page(id - b::F1);
        sel_ = 0;
        loadPending_ = false;
        return true;
    }
    if (id == b::F8) {
        u.inst = uint8_t(clampi(u.inst + (u.shift ? -1 : 1), 1, kInstrumentCount - 1));
        nameCursor_ = 0;
        loadPending_ = false;
        return true;
    }
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
            u.message("Instrument points here");
            edited();
            return true;
        case b::Delete:
            v.clearRow(u.song());
            edited();
            return true;
        default: return false;
    }
}

void InstrumentScreen::labels(const char* out[8]) {
    for (int i = 0; i < kPages; ++i) out[i] = kPageNames[i];
    out[7] = ui().shift ? "INS -" : "INS +";
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
        listRow(g, x, y, w, "NAME", in.name, sel_ == 0);
        if (sel_ == 0) g.hline(x + w - 8 - (int(std::strlen(in.name)) - nameCursor_) * 8, y + 17, 8, col::kCursor);
        y += 24;
        std::snprintf(v, sizeof v, "%02X  %u TICKS%s%s", unsigned(in.gateTimer), unsigned(in.gateTimer & 0x3F),
                      (in.gateTimer & 0x80) ? " NO-HR" : "", (in.gateTimer & 0x40) ? " LEGATO" : "");
        listRow(g, x, y, w, "HR / GATE TIMER", v, sel_ == 1);
        y += 24;
        char wn[32];
        waveName(in.firstWave, wn, sizeof wn);
        std::snprintf(v, sizeof v, "%02X %s", unsigned(in.firstWave), in.firstWave ? wn : "(table)");
        listRow(g, x, y, w, "FIRST WAVE", v, sel_ == 2);
        y += 24;
        std::snprintf(v, sizeof v, in.vibParam ? "SPEED ROW %02X" : "OFF", unsigned(in.vibParam));
        listRow(g, x, y, w, "VIBRATO", v, sel_ == 3);
        y += 24;
        std::snprintf(v, sizeof v, in.vibDelay ? "%u TICKS" : "OFF (no vibrato)", unsigned(in.vibDelay));
        listRow(g, x, y, w, "VIBRATO DELAY", v, sel_ == 4);
        y += 40;
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
            g.text(x, kContentY + 40, kMono, k == tableFocus_ ? col::kText : col::kDim, kTableNames[k]);
            views_[k].kind = TableKind(k);
            views_[k].draw(g, s, x, kContentY + 60, 18, k == tableFocus_, *pointerFor(TableKind(k)));
        }
    } else {
        TableView& v = view();
        v.kind = page_ == kWave ? kWaveTable : (page_ == kPulse ? kPulseTable : kFilterTable);
        const uint8_t ptr = *pointerFor(v.kind);
        std::snprintf(buf, sizeof buf, "%s TABLE  pointer %02X", kTableNames[v.kind], unsigned(ptr));
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
}

// --- SOUNDS page -----------------------------------------------------------------

void InstrumentScreen::soundsEncoder(int d) {
    loadPending_ = false;
    switch (sndRow_) {
        case 0:
            category_ = ((category_ + d) % library::kCategories + library::kCategories) % library::kCategories;
            preset_ = 0;
            break;
        case 1: {
            const int n = library::categoryCount(category_);
            preset_ = ((preset_ + d) % n + n) % n;
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
        case b::Up: sndRow_ = (sndRow_ + 5) % 6; loadPending_ = false; return true;
        case b::Down: sndRow_ = (sndRow_ + 1) % 6; loadPending_ = false; return true;
        case b::EncPush:
        case b::Insert: {
            Song& s = u.song();
            if (sndRow_ <= 1) {
                if (!s.inst[u.inst].empty() && !loadPending_) {
                    loadPending_ = true;
                    u.message("Instrument in use: INSERT again to replace it", 3000);
                    return true;
                }
                loadPending_ = false;
                const library::Preset& p = library::preset(presetIndex());
                const library::Result r = library::load(s, u.inst, p, u.channel % 3);
                u.message(r == library::Result::kOk ? "Preset loaded: play the pads" : "Table full: preset not loaded",
                          2500);
            } else {
                const library::Result r = library::insertArp(s, u.inst, arp_);
                u.message(r == library::Result::kOk ? "Arpeggio written to the wave table" : "Wave table full", 2500);
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
    g.text(x, y, kMono, col::kDim, "SOUND LIBRARY (INSERT loads the preset)");
    y += 20;
    std::snprintf(v, sizeof v, "%s  (%d)", library::categoryName(category_), library::categoryCount(category_));
    listRow(g, x, y, w, "CATEGORY", v, sndRow_ == 0);
    y += 24;
    const library::Preset& p = library::preset(presetIndex());
    std::snprintf(v, sizeof v, "%d/%d  %s", preset_ + 1, library::categoryCount(category_), p.name);
    listRow(g, x, y, w, "PRESET", v, sndRow_ == 1);
    y += 24;
    g.text(x, y, kMono, col::kText, p.text);
    y += 18;
    std::snprintf(v, sizeof v, "AD %02X SR %02X  filter on voice %d", unsigned(p.ad), unsigned(p.sr), u.channel % 3 + 1);
    g.text(x, y, kMono, col::kDim, v);
    y += 36;
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
    if (designer::apply(ui().song(), ui().inst, x, ui().channel % 3) != designer::Result::kOk) {
        ui().message("Table full: change not written", 2500);
        designSlot_ = -1;
        return;
    }
    edited();
    ui().retriggerHeld(ui().inst);
}

bool InstrumentScreen::designButton(uint8_t id) {
    namespace b = ot::btn;
    switch (id) {
        case b::Up: dRow_ = wrap(dRow_ - 1, kDRows); return true;
        case b::Down: dRow_ = wrap(dRow_ + 1, kDRows); return true;
        case b::Left:
        case b::Right:
            dRow_ = dRow_ < kDCol ? clampi(dRow_ + kDCol, kDCol, kDRows - 1) : dRow_ - kDCol;
            return true;
        default: return false;
    }
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
    for (int r = 0; r < kDRows; ++r) {
        const int colX = r < kDCol ? 16 : 410;
        const int y = kContentY + 36 + (r < kDCol ? r : r - kDCol) * 24;
        value(r);
        const bool off = (noFilter && r > kDFilter && r <= kDSweepTicks) ||
                         (x.sweep == designer::kSweepOff && (r == kDSweepSpeed || r == kDSweepTicks)) ||
                         (!x.vibDepth && (r == kDVibSpeed || r == kDVibDelay)) ||
                         (!(x.wave & 0x40) && (r == kDPw || r == kDPwmSpeed || r == kDPwmTicks));
        listRow(g, colX, y, 374, kDNames[r], v, dRow_ == r, off ? col::kDim : col::kText);
    }
    // Graphs: envelope, pulse shape, filter response.
    const int gy = kContentY + 36 + 10 * 24 + 4;
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
                           : "The first change rewrites this instrument's wave / pulse / filter programs.";
    g.text(16, kBarY - 22, kMono, col::kDim, note);
}

}  // namespace ui
}  // namespace sidt
