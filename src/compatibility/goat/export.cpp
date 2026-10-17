// SIDTracker -- GoatTracker export (export.h).
#include "compatibility/goat/export.h"

#include <cstring>

#include "tracker/commands/commands.h"

namespace sidt {
namespace goat {

namespace {

constexpr uint8_t kGtDefaultSpeed = 6;   // GoatTracker starts at tempo 6
constexpr int kMaxGtNote = 93;           // G#7: highest note in a GT pattern

bool referenced(const sidt::Song& s, int pattern) {
    for (int i = 0; i < s.orderLen; ++i)
        if (s.order[i].pattern == pattern) return true;
    return false;
}

int lastInstrument(const sidt::Song& s) {
    int last = 0;
    for (int i = 1; i < kInstrumentCount; ++i) {
        sidt::Instrument def;
        def.clear(i);
        if (!s.inst[i].empty() || std::strcmp(s.inst[i].name, def.name) != 0) last = i;
    }
    return last;
}

Row rowFromCell(const Cell& c, uint32_t& clamped) {
    Row r;
    if (note::isNote(c.note)) {
        int n = c.note;
        if (n > kMaxGtNote) {
            ++clamped;
            n = kMaxGtNote;
        }
        r.note = noteToGt(uint8_t(n));
    } else {
        r.note = noteToGt(c.note);
    }
    r.inst = c.inst;
    r.cmd = uint8_t(c.cmd & 15);
    r.param = r.cmd ? c.param : 0;
    return r;
}

// Rows of `t` reachable from `start` (following jumps), as [lo, hi].
bool reach(const Table& t, uint8_t start, uint8_t& lo, uint8_t& hi) {
    if (!start) return false;
    bool seen[kTableLen + 1] = {};
    lo = hi = start;
    int r = start;
    for (int guard = 0; guard < 2 * kTableLen && r >= 1 && r <= kTableLen && !seen[r]; ++guard) {
        seen[r] = true;
        if (r < lo) lo = uint8_t(r);
        if (r > hi) hi = uint8_t(r);
        if (t.l(r) == 0xFF) {
            r = t.r(r);   // 0 = stop
            continue;
        }
        ++r;
    }
    return true;
}

}  // namespace

ExportCheck checkExport(const sidt::Song& s) {
    ExportCheck c;
    Report& rep = c.report;
    uint32_t sid2 = 0, highNotes = 0, partialStarts = 0, badTranspose = 0;
    for (int p = 0; p < kPatternCount; ++p) {
        if (!referenced(s, p)) continue;
        const Pattern& pat = s.pattern(p);
        if (pat.starts != 0x3F) ++partialStarts;
        for (int r = 0; r < pat.rows; ++r)
            for (int ch = 0; ch < kChannels; ++ch) {
                const Cell& cell = pat.cells[r][ch];
                if (ch >= 3 && !cell.empty()) ++sid2;
                if (ch < 3 && note::isNote(cell.note) && cell.note > kMaxGtNote) ++highNotes;
            }
    }
    for (int i = 0; i < s.orderLen; ++i)
        if (s.order[i].transpose < -15 || s.order[i].transpose > 14) ++badTranspose;
    if (sid2) rep.note("! SID2 tracks 4-6 are not exported (%u cells)", unsigned(sid2));
    if (s.bpm != 125 && s.bpm != 150) rep.note("! %u BPM: GoatTracker plays one row tick per 50 Hz frame", unsigned(s.bpm));
    if (s.bpm == 150) rep.note("! 60 Hz ticks: play with NTSC timing in GoatTracker (-N)");
    if (s.hrAD != 0x0F || s.hrSR != 0x00)
        rep.note("! hard restart ADSR %02X%02X is a GoatTracker option (-A), not saved", unsigned(s.hrAD), unsigned(s.hrSR));
    if (highNotes) {
        rep.note("! %u notes above G#7 lowered to G#7", unsigned(highNotes));
        c.notes = false;
    }
    if (badTranspose) {
        rep.note("! %u transposes outside -15..+14 clamped", unsigned(badTranspose));
        c.orders = false;
    }
    if (partialStarts) rep.note("! imported channel timing: pulse skipping may differ slightly");
    if (s.mode == SongMode::kExtended) rep.note("! Extended song: extended features are not exported");
    if (s.automatedLanes()) rep.note("! Perform automation is not exported");
    if (s.usesDrums()) rep.note("! PCM drum column is not exported");
    if (s.fx.any()) rep.note("! effects are not exported");
    if (s.mod.any()) rep.note("! LFOs and voice pan are not exported");
    rep.note("%s Notes", c.notes ? "ok" : " !");
    rep.note("%s Instruments (%d)", c.instruments ? "ok" : " !", lastInstrument(s));
    rep.note("ok Wave, pulse, filter and speed tables");
    rep.note("%s Order list, transpose, repeat", c.orders ? "ok" : " !");
    rep.ok = c.ok;
    return c;
}

bool exportSong(const sidt::Song& s, Song& gt, Report& rep) {
    rep = Report{};
    gt = Song{};
    std::strncpy(gt.name, s.name, kNameLen);
    std::strncpy(gt.author, s.author, kNameLen);
    gt.subtunes = 1;
    gt.instruments = uint8_t(lastInstrument(s));
    for (int i = 1; i <= gt.instruments; ++i) {
        const sidt::Instrument& in = s.inst[i];
        Instrument& g = gt.inst[i];
        g.ad = in.ad;
        g.sr = in.sr;
        g.wavePtr = in.wavePtr;
        g.pulsePtr = in.pulsePtr;
        g.filterPtr = in.filterPtr;
        g.vibParam = in.vibParam;
        g.vibDelay = in.vibDelay;
        g.gateTimer = in.gateTimer;
        g.firstWave = in.firstWave;
        std::strncpy(g.name, in.name, 16);
        g.name[16] = 0;
    }
    for (int k = 0; k < kTableKinds; ++k) {
        const int used = s.tables[k].used();
        gt.tables.len[k] = uint8_t(used);
        for (int i = 0; i < used; ++i) {
            gt.tables.left[k][i] = s.tables[k].left[i];
            gt.tables.right[k][i] = s.tables[k].right[i];
        }
    }

    // Per channel: GoatTracker patterns start where the SIDTracker pattern
    // marks the track as starting (all tracks for native patterns); rows of
    // a track that continues are appended to its current GT pattern, which
    // restores imported channels' own patterns. A one-pattern item keeps
    // its transpose as a GT transpose command; merged items get the
    // transposes applied to their notes. Identical GT patterns are shared,
    // identical consecutive items become repeats.
    struct Item {
        int16_t pattern;
        int8_t transpose;
        uint8_t repeat;
    };
    static Item items[kOrderMax * 2];
    static Row buf[kPatternRows];
    uint32_t clamped = 0;
    bool tempoSet = s.speed == kGtDefaultSpeed;
    for (int ch = 0; ch < 3; ++ch) {
        int nItems = 0, bufRows = 0, bufPlays = 0, restartItem = 0;
        int8_t bufTranspose = 0;
        bool fail = false;
        auto flush = [&]() {
            if (!bufRows || fail) return;
            int idx = -1;
            for (int g = 0; g < gt.patterns && idx < 0; ++g)
                if (gt.rows[g] == bufRows && std::memcmp(gt.pattern[g], buf, sizeof(Row) * bufRows) == 0) idx = g;
            if (idx < 0) {
                if (gt.patterns >= kPatterns) {
                    fail = true;
                    return;
                }
                idx = gt.patterns++;
                gt.rows[idx] = uint8_t(bufRows);
                std::memcpy(gt.pattern[idx], buf, sizeof(Row) * bufRows);
            }
            const int8_t t = bufPlays == 1 ? bufTranspose : 0;
            if (nItems > 0 && items[nItems - 1].pattern == idx && items[nItems - 1].transpose == t &&
                items[nItems - 1].repeat < 16 && nItems != restartItem) {
                ++items[nItems - 1].repeat;
            } else if (nItems < int(sizeof items / sizeof items[0])) {
                items[nItems++] = Item{int16_t(idx), t, 1};
            } else {
                fail = true;
            }
            bufRows = bufPlays = 0;
        };
        for (int i = 0; i < s.orderLen && !fail; ++i) {
            const OrderEntry& e = s.order[i];
            const Pattern& pat = s.pattern(e.pattern & (kPatternCount - 1));
            const int8_t tr = e.transpose < -15 ? -15 : (e.transpose > 14 ? 14 : e.transpose);
            for (int rep = 0; rep < (e.repeat ? e.repeat : 1) && !fail; ++rep) {
                const bool loopStart = i == s.restartPos && rep == 0;
                const bool start = ((pat.starts >> ch) & 1) || loopStart || bufRows + pat.rows > kPatternRows;
                if (start) {
                    flush();
                    if (loopStart) restartItem = nItems;
                }
                // Merged plays get their transposes applied to the notes.
                if (bufPlays == 1) {
                    for (int r = 0; r < bufRows; ++r)
                        if (buf[r].note >= kNoteFirst && buf[r].note <= kNoteLast) {
                            int n = buf[r].note + bufTranspose;
                            buf[r].note = uint8_t(n < kNoteFirst ? kNoteFirst : (n > kNoteLast ? kNoteLast : n));
                        }
                }
                for (int r = 0; r < pat.rows; ++r) {
                    Row row = rowFromCell(pat.cells[r][ch], clamped);
                    if (bufPlays >= 1 && row.note >= kNoteFirst && row.note <= kNoteLast) {
                        int n = row.note + tr;
                        row.note = uint8_t(n < kNoteFirst ? kNoteFirst : (n > kNoteLast ? kNoteLast : n));
                    }
                    // GT has no initial speed: the song's first row gets Fxx
                    // on the first track with a free command there.
                    if (!tempoSet && i == 0 && rep == 0 && r == 0 && row.cmd == 0) {
                        row.cmd = cmd::kTempo;
                        row.param = s.speed;
                        tempoSet = true;
                    }
                    buf[bufRows++] = row;
                }
                if (bufPlays == 0) bufTranspose = tr;
                ++bufPlays;
            }
        }
        flush();
        if (fail) {
            rep.note("needs more than %d GoatTracker patterns or order entries", kPatterns);
            return false;
        }
        // Order bytes: transpose when it changes (and at the loop start,
        // since GoatTracker keeps the transpose when it loops), repeat, pattern.
        uint8_t* o = gt.order[0][ch];
        int n = 0;
        int8_t cur = 0;
        uint8_t restartByte = 0;
        for (int i = 0; i < nItems; ++i) {
            const Item& it = items[i];
            if (i == restartItem) restartByte = uint8_t(n);
            if (it.transpose != cur || (i == restartItem && i > 0)) {
                o[n++] = uint8_t(0xF0 + it.transpose);
                cur = it.transpose;
            }
            if (it.repeat > 1) o[n++] = uint8_t(kOrderRepeat + ((it.repeat - 1) & 15));
            o[n++] = uint8_t(it.pattern);
            if (n > kOrderMax - 6) {
                rep.note("order list longer than GoatTracker's 254 entries");
                return false;
            }
        }
        o[n++] = kOrderEnd;
        gt.orderLen[0][ch] = uint8_t(n);
        gt.orderRestart[0][ch] = restartByte;
    }
    if (!tempoSet) rep.note("! speed %u could not be set (no free command on the first row)", unsigned(s.speed));
    rep.ok = true;
    if (clamped) rep.note("! %u notes above G#7 lowered", unsigned(clamped));
    rep.note("%u GoatTracker patterns, %u instruments", unsigned(gt.patterns), unsigned(gt.instruments));
    return true;
}

bool exportInstrument(const sidt::Song& s, int slot, InsFile& out) {
    out = InsFile{};
    if (slot < 1 || slot >= kInstrumentCount) return false;
    const sidt::Instrument& in = s.inst[slot];
    uint8_t lo[kTableKinds] = {}, hi[kTableKinds] = {};
    bool has[kTableKinds] = {};
    has[kWaveTable] = reach(s.tables[kWaveTable], in.wavePtr, lo[kWaveTable], hi[kWaveTable]);
    has[kPulseTable] = reach(s.tables[kPulseTable], in.pulsePtr, lo[kPulseTable], hi[kPulseTable]);
    has[kFilterTable] = reach(s.tables[kFilterTable], in.filterPtr, lo[kFilterTable], hi[kFilterTable]);
    // Speed rows: the instrument vibrato and wave table commands 1..4 / E.
    auto speedRow = [&](uint8_t r) {
        if (!r) return;
        if (!has[kSpeedTable]) {
            lo[kSpeedTable] = hi[kSpeedTable] = r;
            has[kSpeedTable] = true;
        }
        if (r < lo[kSpeedTable]) lo[kSpeedTable] = r;
        if (r > hi[kSpeedTable]) hi[kSpeedTable] = r;
    };
    speedRow(in.vibParam);
    const Table& w = s.tables[kWaveTable];
    for (int r = lo[kWaveTable]; has[kWaveTable] && r <= hi[kWaveTable]; ++r) {
        const uint8_t L = w.l(r);
        if (L >= 0xF1 && (cmd::usesSpeedTable(uint8_t(L & 15)))) speedRow(w.r(r));
    }
    for (int k = 0; k < kTableKinds; ++k) {
        if (!has[k]) continue;
        const Table& t = s.tables[k];
        out.tables.len[k] = uint8_t(hi[k] - lo[k] + 1);
        for (int r = lo[k]; r <= hi[k]; ++r) {
            uint8_t L = t.l(r), R = t.r(r);
            if (k != kSpeedTable && L == 0xFF && R) R = uint8_t(R - lo[k] + 1);   // jumps
            if (k == kWaveTable && L >= 0xF1 && cmd::usesSpeedTable(uint8_t(L & 15)) && R) R = uint8_t(R - lo[kSpeedTable] + 1);
            out.tables.left[k][r - lo[k]] = L;
            out.tables.right[k][r - lo[k]] = R;
        }
    }
    Instrument& g = out.inst;
    g.ad = in.ad;
    g.sr = in.sr;
    g.wavePtr = has[kWaveTable] ? uint8_t(in.wavePtr - lo[kWaveTable] + 1) : 0;
    g.pulsePtr = has[kPulseTable] ? uint8_t(in.pulsePtr - lo[kPulseTable] + 1) : 0;
    g.filterPtr = has[kFilterTable] ? uint8_t(in.filterPtr - lo[kFilterTable] + 1) : 0;
    g.vibParam = in.vibParam ? uint8_t(in.vibParam - lo[kSpeedTable] + 1) : 0;
    g.vibDelay = in.vibDelay;
    g.gateTimer = in.gateTimer;
    g.firstWave = in.firstWave;
    std::strncpy(g.name, in.name, 16);
    g.name[16] = 0;
    return true;
}

}  // namespace goat
}  // namespace sidt
