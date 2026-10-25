// SIDTracker -- GoatTracker import (import.h). No heap: the order lists are
// walked by small cursors, row by row.
#include "compatibility/goat/import.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "tracker/commands/commands.h"

namespace sidt {
namespace goat {

namespace {

constexpr uint32_t kMaxSongRows = 16384;   // flattened song length cap (intro + one loop)

// One GoatTracker channel's order list, played row by row.
class Cursor {
public:
    Cursor(const Song& s, int sub, int ch) : s_(s), sub_(sub), ch_(ch) {}
    // Advance to the next row; returns false on an order list without
    // playable patterns.
    bool step() {
        if (started_ && ++row_ < s_.rows[pattern_]) {
            itemStart_ = false;
            return true;
        }
        started_ = true;
        row_ = 0;
        itemStart_ = true;
        if (repeatLeft_ > 1) {
            --repeatLeft_;
            return true;
        }
        const uint8_t* o = s_.order[sub_][ch_];
        const int len = s_.orderLen[sub_][ch_];
        uint8_t repeat = 1;
        for (int guard = 0; guard < 4 * kOrderMax; ++guard) {
            if (idx_ >= len) idx_ = 0;
            const uint8_t b = o[idx_];
            if (b == kOrderEnd) {
                idx_ = s_.orderRestart[sub_][ch_] < len - 1 ? s_.orderRestart[sub_][ch_] : 0;
                ++wraps_;
                continue;
            }
            ++idx_;
            if (b >= kOrderTranspose) {
                transpose_ = int8_t(int(b) - 0xF0);
            } else if (b >= kOrderRepeat) {
                // Measured: the pattern after Dn plays n + 1 times (D0: once).
                repeat = uint8_t((b & 15) + 1);
            } else {
                pattern_ = b < s_.patterns ? b : 0;
                repeatLeft_ = repeat;
                if (!s_.rows[pattern_]) continue;   // empty pattern: next item
                return true;
            }
        }
        return false;
    }
    const Row& row() const { return s_.pattern[pattern_][row_]; }
    int8_t transpose() const { return transpose_; }
    bool itemStart() const { return itemStart_; }
    uint32_t wraps() const { return wraps_; }

private:
    const Song& s_;
    int sub_, ch_;
    int idx_ = 0;
    uint8_t pattern_ = 0, row_ = 0, repeatLeft_ = 0;
    int8_t transpose_ = 0;
    bool started_ = false, itemStart_ = false;
    uint32_t wraps_ = 0;
};

// Intro rows and loop rows of one channel: the loop state repeats once the
// transpose at the restart point is the same twice in a row.
bool channelLoop(const Song& s, int sub, int ch, uint32_t& intro, uint32_t& period) {
    Cursor c(s, sub, ch);
    uint32_t rows = 0, wrapRow[4] = {}, seen = 0;
    int8_t wrapT[4] = {};
    // A wrap is visible on the first row after it (that row starts the loop).
    while (rows < 8 * kMaxSongRows) {
        const uint32_t before = c.wraps();
        if (!c.step()) return false;
        if (c.wraps() != before) {
            wrapRow[seen] = rows;
            wrapT[seen] = c.transpose();
            if (seen >= 1 && wrapT[seen] == wrapT[seen - 1]) {
                intro = wrapRow[seen - 1];
                period = wrapRow[seen] - wrapRow[seen - 1];
                if (!period) return false;
                // The rows often repeat before the first restart (the restart
                // position need not be the first order entry): the earliest
                // row from which row r == row r + period for good.
                Cursor a(s, sub, ch), b(s, sub, ch);
                for (uint32_t i = 0; i < period; ++i) b.step();
                uint32_t lastDiff = 0;
                bool any = false;
                for (uint32_t r = 0; r < intro; ++r) {
                    a.step();
                    b.step();
                    const Row &x = a.row(), &y = b.row();
                    if (x.note != y.note || x.inst != y.inst || x.cmd != y.cmd || x.param != y.param ||
                        a.transpose() != b.transpose() || a.itemStart() != b.itemStart()) {
                        lastDiff = r;
                        any = true;
                    }
                }
                intro = any ? lastDiff + 1 : 0;
                return true;
            }
            if (++seen == 4) return false;
        }
        ++rows;
    }
    return false;
}

uint64_t gcd(uint64_t a, uint64_t b) {
    while (b) {
        const uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

bool sameCells(const Pattern& a, const Pattern& b) {
    if (a.rows != b.rows || a.starts != b.starts) return false;
    return std::memcmp(a.cells, b.cells, sizeof(Cell) * kChannels * a.rows) == 0;
}

void copyName(char* dst, const char* src, int n) {
    int i = 0;
    for (; i < n && src[i]; ++i) dst[i] = (src[i] >= 32 && src[i] < 127) ? src[i] : '?';
    dst[i] = 0;
    while (i > 0 && dst[i - 1] == ' ') dst[--i] = 0;
}

void instrumentFromGt(const Instrument& g, sidt::Instrument& out, int index) {
    out.clear(index);
    out.ad = g.ad;
    out.sr = g.sr;
    out.wavePtr = g.wavePtr;
    out.pulsePtr = g.pulsePtr;
    out.filterPtr = g.filterPtr;
    out.vibParam = g.vibParam;
    out.vibDelay = g.vibDelay;
    out.gateTimer = g.gateTimer;
    out.firstWave = g.firstWave;
    if (g.name[0]) copyName(out.name, g.name, 16);
}

}  // namespace

void Report::note(const char* fmt, ...) {
    if (lines >= kLines) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line[lines++], sizeof line[0], fmt, ap);
    va_end(ap);
}

uint8_t noteFromGt(uint8_t n) {
    if (n >= kNoteFirst && n <= kNoteLast) return uint8_t(n - kNoteFirst + 1);
    if (n == kKeyOff) return note::kKeyOff;
    if (n == kKeyOn) return note::kKeyOn;
    return note::kEmpty;
}

uint8_t noteToGt(uint8_t n) {
    if (note::isNote(n)) return uint8_t(kNoteFirst + n - 1);   // A-7..B-7 exceed G#7: caller checks
    if (n == note::kKeyOff) return kKeyOff;
    if (n == note::kKeyOn) return kKeyOn;
    return kRest;
}

bool importSong(const Song& gt, int sub, sidt::Song& out, Report& rep) {
    rep = Report{};
    out.clear();
    if (sub < 0 || sub >= gt.subtunes) sub = 0;
    copyName(out.name, gt.name, sidt::kNameLen);
    copyName(out.author, gt.author, sidt::kNameLen);
    if (!out.name[0]) std::strcpy(out.name, "GT IMPORT");
    // GoatTracker defaults (readme §2.1): PAL, 6581, hard restart ADSR 0F00,
    // tempo 6. The hard-restart ADSR is an editor option, not song data.
    out.bpm = 125;
    out.speed = 6;
    out.hrAD = 0x0F;
    out.hrSR = 0x00;
    out.sid.model[0] = out.sid.model[1] = sid::Model::k6581;
    out.sid.clock = SidClock::kPal;
    out.sid.balance = 0;
    for (int i = 1; i < kInstrumentCount; ++i) {
        if (i <= gt.instruments) {
            instrumentFromGt(gt.inst[i], out.inst[i], i);
        } else {
            // GoatTracker plays a slot above the stored count as a zero
            // (silent) instrument: keep that, not the audible init patch.
            out.inst[i].clear(i);
            out.inst[i].ad = sidt::Instrument::kLegacyAd;
            out.inst[i].sr = sidt::Instrument::kLegacySr;
            out.inst[i].firstWave = sidt::Instrument::kLegacyWave;
        }
    }
    for (int k = 0; k < kTableKinds; ++k) {
        out.tables[k] = sidt::Table{};
        for (int i = 0; i < gt.tables.len[k]; ++i) {
            out.tables[k].left[i] = gt.tables.left[k][i];
            out.tables[k].right[i] = gt.tables.right[k][i];
        }
    }

    // Command statistics over the source patterns (PLAN.md §8 report).
    uint32_t perChannelTempo = 0, timingMarks = 0, noteIndep = 0;
    const Table& speed = out.tables[kSpeedTable];
    for (int p = 0; p < gt.patterns; ++p)
        for (int r = 0; r < gt.rows[p]; ++r) {
            const Row& row = gt.pattern[p][r];
            const uint8_t c = row.cmd & 15;
            if (!c) continue;
            ++rep.commands;
            if (c == cmd::kTempo && row.param >= 0x83) {
                ++perChannelTempo;
                ++rep.approximated;
            } else if (c == cmd::kVolume && (row.param & 0xF0)) {
                ++timingMarks;
                ++rep.unsupported;
            } else if ((c >= cmd::kPortaUp && c <= cmd::kVibrato) && row.param && (speed.l(row.param) & 0x80)) {
                ++noteIndep;
                ++rep.approximated;
            } else {
                ++rep.direct;
            }
        }

    // Loop structure of the three channels.
    uint32_t intro = 0, maxPeriod = 0;
    uint64_t period = 1;
    for (int ch = 0; ch < 3; ++ch) {
        uint32_t i = 0, p = 0;
        if (!channelLoop(gt, sub, ch, i, p)) {
            rep.note("channel %d: order list has no playable pattern", ch + 1);
            out.clear();
            return false;
        }
        if (i > intro) intro = i;
        if (p > maxPeriod) maxPeriod = p;
        period = period / gcd(period, p) * p;
    }

    // Walk `total` rows, cut patterns where any channel starts a pattern
    // (and at the loop start), share identical cuts. False when it fails;
    // `capacity`: out of patterns or order entries.
    int patterns = 0;
    uint32_t clamped = 0;
    bool capacity = false;
    auto flatten = [&](uint32_t total) -> bool {
        Cursor cur[3] = {Cursor(gt, sub, 0), Cursor(gt, sub, 1), Cursor(gt, sub, 2)};
        capacity = false;
        Pattern seg;
        seg.clear(1);
        seg.rows = 0;
        patterns = 0;
        clamped = 0;
        uint8_t restartPos = 0;
        bool fail = false;
        auto flush = [&]() {
            if (!seg.rows || fail) return;
            int idx = -1;
            for (int p = 0; p < patterns && idx < 0; ++p)
                if (sameCells(out.pattern(p), seg)) idx = p;
            if (idx < 0) {
                if (patterns >= kPatternCount) {
                    fail = capacity = true;
                    return;
                }
                idx = patterns++;
                out.pattern(idx) = seg;
            }
            if (out.orderLen >= kOrderLen - 1) {
                fail = capacity = true;
                return;
            }
            OrderEntry& e = out.order[out.orderLen++];
            e.pattern = uint8_t(idx);
            e.transpose = 0;
            e.repeat = 1;
            seg.clear(1);
            seg.rows = 0;
        };
        out.orderLen = 0;
        for (uint32_t r = 0; r < total && !fail; ++r) {
            bool boundary = r == intro;
            for (int ch = 0; ch < 3; ++ch) {
                if (!cur[ch].step()) fail = true;
                boundary = boundary || cur[ch].itemStart();
            }
            if (boundary || seg.rows >= kMaxRows) {
                flush();
                if (r == intro) restartPos = out.orderLen;
                seg.starts = 0;
                for (int ch = 0; ch < 3; ++ch)
                    if (cur[ch].itemStart()) seg.starts = uint8_t(seg.starts | (1u << ch));
            }
            Cell* row = seg.cells[seg.rows];
            for (int ch = 0; ch < 3; ++ch) {
                const Row& g = cur[ch].row();
                Cell c;
                c.note = noteFromGt(g.note);
                if (note::isNote(c.note)) {
                    int n = c.note + cur[ch].transpose();
                    if (n < note::kFirst || n > note::kLast) {
                        ++clamped;
                        n = n < note::kFirst ? note::kFirst : note::kLast;
                    }
                    c.note = uint8_t(n);
                }
                c.inst = g.inst < kInstrumentCount ? g.inst : 0;
                c.cmd = uint8_t(g.cmd & 15);
                c.param = c.cmd ? g.param : 0;
                row[ch] = c;
            }
            for (int ch = 3; ch < kChannels; ++ch) row[ch] = Cell{};
            ++seg.rows;
        }
        flush();
        if (fail || !out.orderLen) return false;
        // Consecutive identical entries -> repeats (never across the loop start).
        int w = 0;
        uint8_t newRestart = 0;
        for (int i = 0; i < out.orderLen; ++i) {
            if (i == restartPos) newRestart = uint8_t(w);
            if (w > 0 && i != restartPos && out.order[w - 1].pattern == out.order[i].pattern &&
                out.order[w - 1].repeat < 16) {
                ++out.order[w - 1].repeat;
                continue;
            }
            out.order[w++] = out.order[i];
        }
        out.orderLen = uint8_t(w);
        out.restartPos = newRestart < out.orderLen ? newRestart : 0;
        return true;
    };
    // Exact loop first: intro + least common multiple of the channel loops.
    // GoatTracker channels whose loops differ in length drift apart; when
    // the exact loop does not fit, one pass of the longest channel loop is
    // used and the song loop brings the channels back in step.
    bool resync = false;
    uint32_t total = intro + uint32_t(period);
    bool ok = intro + period <= kMaxSongRows && flatten(total);
    if (!ok && (capacity || intro + period > kMaxSongRows) && maxPeriod < period) {
        resync = true;
        total = intro + maxPeriod;
        ok = flatten(total);
    }
    if (!ok) {
        if (capacity) rep.note("needs more than %d patterns or %d order entries", kPatternCount, kOrderLen - 1);
        else rep.note("order lists could not be followed");
        out.clear();
        return false;
    }

    rep.ok = true;
    rep.note("%u%% commands mapped directly (%u of %u)", unsigned(rep.directPercent()), unsigned(rep.direct),
             unsigned(rep.commands));
    if (rep.approximated) rep.note("%u commands approximated", unsigned(rep.approximated));
    if (rep.unsupported) rep.note("%u commands unsupported (ignored)", unsigned(rep.unsupported));
    if (perChannelTempo) rep.note("! F83-FFF per-channel tempo applied to all tracks (%u)", unsigned(perChannelTempo));
    if (timingMarks) rep.note("! DXY timing marks ignored (%u)", unsigned(timingMarks));
    if (noteIndep) rep.note("! note-independent speeds approximated (%u)", unsigned(noteIndep));
    if (clamped) rep.note("! %u transposed notes outside C-0..B-7 clamped", unsigned(clamped));
    if (resync) rep.note("! channel loops differ (%u..%u rows): channels re-aligned at the loop", unsigned(maxPeriod), unsigned(period));
    if (gt.subtunes > 1) rep.note("subtune %d of %u imported", sub + 1, unsigned(gt.subtunes));
    rep.note("%u patterns, %u order entries, %u rows, loop at row %u", unsigned(patterns), unsigned(out.orderLen),
             unsigned(total), unsigned(intro));
    return true;
}

bool importInstrument(const InsFile& ins, int slot, sidt::Song& out, Report& rep) {
    rep = Report{};
    if (slot < 1 || slot >= kInstrumentCount) return false;
    uint8_t base[kTableKinds];
    for (int k = 0; k < kTableKinds; ++k) {
        base[k] = uint8_t(out.tables[k].used());
        if (base[k] + ins.tables.len[k] > kTableLen) {
            rep.note("not enough free rows in the %s table", k == 0 ? "wave" : k == 1 ? "pulse" : k == 2 ? "filter" : "speed");
            return false;
        }
    }
    for (int k = 0; k < kTableKinds; ++k) {
        Table& t = out.tables[k];
        for (int i = 0; i < ins.tables.len[k]; ++i) {
            uint8_t L = ins.tables.left[k][i], R = ins.tables.right[k][i];
            if (k != kSpeedTable && L == 0xFF && R) R = uint8_t(R + base[k]);   // relocate jumps
            t.left[base[k] + i] = L;
            t.right[base[k] + i] = R;
        }
    }
    instrumentFromGt(ins.inst, out.inst[slot], slot);
    sidt::Instrument& in = out.inst[slot];
    auto reloc = [&](uint8_t& p, int k) {
        if (p) p = uint8_t(p + base[k]);
    };
    reloc(in.wavePtr, kWaveTable);
    reloc(in.pulsePtr, kPulseTable);
    reloc(in.filterPtr, kFilterTable);
    reloc(in.vibParam, kSpeedTable);
    rep.ok = true;
    rep.note("instrument %02X: %s", unsigned(slot), in.name);
    return true;
}

}  // namespace goat
}  // namespace sidt
