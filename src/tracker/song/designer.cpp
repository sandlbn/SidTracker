// SIDTracker -- sound designer (designer.h). Table semantics: docs/commands.md.
#include "tracker/song/designer.h"

namespace sidt {
namespace designer {

namespace {

constexpr int kWaveRows = 3, kPulseRows = 4, kFilterRows = 5;
const uint8_t kWaves[] = {0x10, 0x20, 0x40, 0x80, 0x30, 0x50, 0x60, 0x70};
const char* const kWaveNames[] = {"TRIANGLE", "SAW", "PULSE", "NOISE", "SAW+TRI", "PULSE+TRI", "PULSE+SAW", "P+S+T"};
const char* const kTransientNames[kTransients] = {"NONE", "NOISE CLICK", "OCTAVE BLIP", "FIFTH BLIP"};
const char* const kFilterNames[kFilterTypes] = {"OFF", "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH"};
const uint8_t kFilterBits[kFilterTypes] = {0x00, 0x10, 0x20, 0x40, 0x50};
const char* const kSweepNames[kSweeps] = {"OFF", "DOWN", "UP", "WOBBLE"};

struct Row {
    uint8_t l, r;
};

void waveRows(const Design& d, Row* w) {
    const uint8_t main = uint8_t((d.wave & 0xF0) | 0x01);
    switch (d.transient) {
        case kClick: w[0] = {0x81, 0x18}; break;
        case kOctave: w[0] = {main, 0x0C}; break;
        case kFifth: w[0] = {main, 0x07}; break;
        default: w[0] = {main, 0x00}; break;
    }
    w[1] = {main, 0x00};
    w[2] = {0xFF, 0x00};
}

void pulseRows(const Design& d, Row* p, int start) {
    const uint8_t ticks = uint8_t(d.pwmTicks < 1 ? 1 : (d.pwmTicks > 0x7F ? 0x7F : d.pwmTicks));
    p[0] = {uint8_t(0x80 | ((d.pw >> 8) & 0x0F)), uint8_t(d.pw)};
    p[1] = {ticks, d.pwmSpeed};
    p[2] = {ticks, uint8_t(-int(d.pwmSpeed))};
    p[3] = {0xFF, uint8_t(start + 1)};
}

void filterRows(const Design& d, Row* f, int start, int voice) {
    const uint8_t ticks = uint8_t(d.sweepTicks < 1 ? 1 : (d.sweepTicks > 0x7F ? 0x7F : d.sweepTicks));
    const uint8_t spd = d.sweepSpeed;
    f[0] = {uint8_t(0x80 | kFilterBits[d.filter % kFilterTypes]), uint8_t((d.resonance << 4) | (1 << (voice % 3)))};
    f[1] = {0x00, d.cutoff};
    f[2] = {0x01, 0x00};
    f[3] = {0x01, 0x00};
    f[4] = {0xFF, 0x00};
    switch (d.sweep) {
        case kSweepDown: f[2] = {ticks, uint8_t(-int(spd))}; break;
        case kSweepUp: f[2] = {ticks, spd}; break;
        case kWobble:
            f[2] = {ticks, spd};
            f[3] = {ticks, uint8_t(-int(spd))};
            f[4] = {0xFF, uint8_t(start + 2)};
            break;
        default: break;
    }
}

Row at(const Table& t, int row) { return {t.l(row), t.r(row)}; }

bool isWave(const Table& t, int p) {
    if (!p || p + kWaveRows - 1 > kTableLen) return false;
    const Row m = at(t, p + 1), e = at(t, p + 2);
    return e.l == 0xFF && e.r == 0 && (m.l & 0x01) && (m.l & 0xF0) && m.l < 0xE0 && m.r == 0;
}

bool isPulse(const Table& t, int p) {
    if (!p || p + kPulseRows - 1 > kTableLen) return false;
    const Row a = at(t, p), b = at(t, p + 1), c = at(t, p + 2), e = at(t, p + 3);
    return a.l >= 0x80 && a.l != 0xFF && b.l == c.l && b.l >= 1 && b.l <= 0x7F && uint8_t(b.r + c.r) == 0 &&
           e.l == 0xFF && e.r == p + 1;
}

bool isFilter(const Table& t, int p) {
    if (!p || p + kFilterRows - 1 > kTableLen) return false;
    const Row a = at(t, p), b = at(t, p + 1), e = at(t, p + 4);
    return a.l >= 0x80 && a.l != 0xFF && b.l == 0 && e.l == 0xFF && (e.r == 0 || e.r == p + 2);
}

uint8_t* pointer(Instrument& in, TableKind k) {
    return k == kWaveTable ? &in.wavePtr : (k == kPulseTable ? &in.pulsePtr : &in.filterPtr);
}

// True when a pattern command (8xx wave, 9xx pulse, Axx filter pointer) or
// a wave-table command row (F8 / F9 / FA) points into rows [p, p + n) of
// table k: such rows are shared even when no other instrument uses them.
bool commandRefs(const Song& s, TableKind k, int p, int n) {
    const uint8_t want = k == kWaveTable ? 0x8 : (k == kPulseTable ? 0x9 : 0xA);
    auto inside = [&](int r) { return r >= p && r < p + n; };
    const Table& w = s.tables[kWaveTable];
    for (int r = 1; r <= w.used(); ++r)
        if (w.l(r) == (0xF0 | want) && inside(w.r(r))) return true;
    if (!s.patterns) return false;
    for (int q = 0; q < kPatternCount; ++q) {
        const Pattern& pt = s.pattern(q);
        for (int r = 0; r < pt.rows; ++r)
            for (int ch = 0; ch < kChannels; ++ch) {
                const Cell& c = pt.cells[r][ch];
                if ((c.cmd & 15) == want && inside(c.param)) return true;
            }
    }
    return false;
}

// Rows for a program of n rows: the instrument's own block when it has the
// designer's shape and no other instrument points into it, else after the
// used rows. 0 = no room.
int block(const Song& s, TableKind k, int slot, bool shaped, int n) {
    const uint8_t p = *pointer(const_cast<Instrument&>(s.inst[slot]), k);
    if (shaped) {
        bool own = true;
        for (int i = 1; i < kInstrumentCount && own; ++i) {
            if (i == slot) continue;
            const uint8_t q = *pointer(const_cast<Instrument&>(s.inst[i]), k);
            own = !(q >= p && q < p + n);
        }
        if (own && !commandRefs(s, k, p, n)) return p;
    }
    const int start = s.tables[k].used() + 1;
    return start + n - 1 <= kTableLen ? start : 0;
}

bool pointedAt(const Song& s, TableKind k, int row, int except) {
    for (int i = 1; i < kInstrumentCount; ++i)
        if (i != except && *pointer(const_cast<Instrument&>(s.inst[i]), k) == row) return true;
    return false;
}

// A designer filter block nobody uses (left by switching the filter off).
int orphanFilter(const Song& s, int slot) {
    const Table& t = s.tables[kFilterTable];
    for (int r = 1; r + kFilterRows - 1 <= t.used(); ++r)
        if (isFilter(t, r) && !pointedAt(s, kFilterTable, r, slot)) {
            bool inside = false;   // not the tail of another instrument's program
            for (int i = 1; i < kInstrumentCount && !inside; ++i) {
                const int q = s.inst[i].filterPtr;
                inside = q && q < r && r < q + kFilterRows;
            }
            if (!inside && !commandRefs(s, kFilterTable, r, kFilterRows)) return r;
        }
    return 0;
}

// Speed row `row` is used by another instrument's vibrato or by a pattern
// command that reads the speed table (1..4, E).
bool speedRowShared(const Song& s, int row, int slot) {
    for (int i = 1; i < kInstrumentCount; ++i)
        if (i != slot && s.inst[i].vibParam == row) return true;
    if (!s.patterns) return false;
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pt = s.pattern(p);
        for (int r = 0; r < pt.rows; ++r)
            for (int ch = 0; ch < kChannels; ++ch) {
                const Cell& c = pt.cells[r][ch];
                const uint8_t cm = uint8_t(c.cmd & 15);
                if (((cm >= 1 && cm <= 4) || cm == 0xE) && c.param == row) return true;
            }
    }
    return false;
}

void put(Table& t, const Row* rows, int n, int start) {
    for (int i = 0; i < n; ++i) {
        t.left[start + i - 1] = rows[i].l;
        t.right[start + i - 1] = rows[i].r;
    }
}

}  // namespace

const char* waveName(uint8_t wave) {
    for (int i = 0; i < int(sizeof kWaves); ++i)
        if (kWaves[i] == (wave & 0xF0)) return kWaveNames[i];
    return "?";
}
const char* transientName(int t) { return t >= 0 && t < kTransients ? kTransientNames[t] : "?"; }
const char* filterName(int f) { return f >= 0 && f < kFilterTypes ? kFilterNames[f] : "?"; }
const char* sweepName(int s) { return s >= 0 && s < kSweeps ? kSweepNames[s] : "?"; }

int waveChoices(const uint8_t** out) {
    *out = kWaves;
    return int(sizeof kWaves);
}

bool isDesigned(const Song& s, int slot) {
    if (slot < 1 || slot >= kInstrumentCount) return false;
    const Instrument& in = s.inst[slot];
    return isWave(s.tables[kWaveTable], in.wavePtr) && isPulse(s.tables[kPulseTable], in.pulsePtr);
}

Design decode(const Song& s, int slot) {
    Design d;
    if (slot < 1 || slot >= kInstrumentCount) return d;
    const Instrument& in = s.inst[slot];
    d.attack = uint8_t(in.ad >> 4);
    d.decay = uint8_t(in.ad & 15);
    d.sustain = uint8_t(in.sr >> 4);
    d.release = uint8_t(in.sr & 15);
    d.hardRestart = !(in.gateTimer & 0x80);
    const Table& w = s.tables[kWaveTable];
    if (isWave(w, in.wavePtr)) {
        const Row t = at(w, in.wavePtr), m = at(w, in.wavePtr + 1);
        d.wave = uint8_t(m.l & 0xF0);
        if (t.l == 0x81 && t.r == 0x18) d.transient = kClick;
        else if (t.r == 0x0C) d.transient = kOctave;
        else if (t.r == 0x07) d.transient = kFifth;
    } else if (in.wavePtr && (w.l(in.wavePtr) & 0xF0) && w.l(in.wavePtr) < 0xE0) {
        d.wave = uint8_t(w.l(in.wavePtr) & 0xF0);
    }
    const Table& p = s.tables[kPulseTable];
    if (isPulse(p, in.pulsePtr)) {
        const Row a = at(p, in.pulsePtr), b = at(p, in.pulsePtr + 1);
        d.pw = uint16_t(((a.l & 0x0F) << 8) | a.r);
        d.pwmTicks = b.l;
        d.pwmSpeed = b.r;
    } else if (in.pulsePtr && p.l(in.pulsePtr) >= 0x80 && p.l(in.pulsePtr) != 0xFF) {
        d.pw = uint16_t(((p.l(in.pulsePtr) & 0x0F) << 8) | p.r(in.pulsePtr));
    }
    const Table& f = s.tables[kFilterTable];
    if (in.filterPtr && isFilter(f, in.filterPtr)) {
        const int q = in.filterPtr;
        const Row a = at(f, q), b = at(f, q + 1), x = at(f, q + 2), e = at(f, q + 4);
        for (int i = 1; i < kFilterTypes; ++i)
            if ((a.l & 0x70) == kFilterBits[i]) d.filter = uint8_t(i);
        d.resonance = uint8_t(a.r >> 4);
        d.cutoff = b.r;
        if (e.r) {
            d.sweep = kWobble;
            d.sweepTicks = x.l;
            d.sweepSpeed = x.r;
        } else if (x.r && x.l > 0) {
            d.sweep = int8_t(x.r) < 0 ? kSweepDown : kSweepUp;
            d.sweepTicks = x.l;
            d.sweepSpeed = uint8_t(int8_t(x.r) < 0 ? -int(int8_t(x.r)) : x.r);
        }
    }
    if (in.vibParam && in.vibDelay) {
        const Table& sp = s.tables[kSpeedTable];
        d.vibSpeed = sp.l(in.vibParam);
        d.vibDepth = sp.r(in.vibParam);
        d.vibDelay = in.vibDelay;
    } else {
        d.vibDepth = 0;
    }
    return d;
}

Result apply(Song& s, int slot, const Design& d, int voice) {
    if (slot < 1 || slot >= kInstrumentCount) return Result::kTableFull;
    Instrument& in = s.inst[slot];
    const int sw = block(s, kWaveTable, slot, isWave(s.tables[kWaveTable], in.wavePtr), kWaveRows);
    const int sp = block(s, kPulseTable, slot, isPulse(s.tables[kPulseTable], in.pulsePtr), kPulseRows);
    const bool filt = d.filter != kFilterOff;
    int sf = -1;
    if (filt) {
        const bool shaped = in.filterPtr && isFilter(s.tables[kFilterTable], in.filterPtr);
        sf = shaped ? block(s, kFilterTable, slot, true, kFilterRows) : orphanFilter(s, slot);
        if (!sf) sf = block(s, kFilterTable, slot, false, kFilterRows);
    }
    // Vibrato: an existing identical speed row, else a new one.
    int sv = -1;
    const bool vib = d.vibDepth != 0;
    if (vib) {
        const Table& t = s.tables[kSpeedTable];
        sv = 0;
        for (int r = 1; r <= t.used() && !sv; ++r)
            if (t.l(r) == d.vibSpeed && t.r(r) == d.vibDepth) sv = r;
        // The instrument's own row, rewritten when nothing else reads it.
        if (!sv && in.vibParam && !speedRowShared(s, in.vibParam, slot)) sv = in.vibParam;
        if (!sv && t.used() < kTableLen) sv = t.used() + 1;
    }
    if (!sw || !sp || !sf || !sv) return Result::kTableFull;
    Row w[kWaveRows], p[kPulseRows], f[kFilterRows];
    waveRows(d, w);
    put(s.tables[kWaveTable], w, kWaveRows, sw);
    pulseRows(d, p, sp);
    put(s.tables[kPulseTable], p, kPulseRows, sp);
    if (filt) {
        filterRows(d, f, sf, voice);
        put(s.tables[kFilterTable], f, kFilterRows, sf);
    }
    if (vib) {
        s.tables[kSpeedTable].left[sv - 1] = d.vibSpeed;
        s.tables[kSpeedTable].right[sv - 1] = d.vibDepth;
    }
    in.ad = uint8_t((d.attack << 4) | (d.decay & 15));
    in.sr = uint8_t((d.sustain << 4) | (d.release & 15));
    in.firstWave = 0x09;
    in.wavePtr = uint8_t(sw);
    in.pulsePtr = uint8_t(sp);
    in.filterPtr = uint8_t(filt ? sf : 0);
    in.vibParam = uint8_t(vib ? sv : 0);
    in.vibDelay = vib ? (d.vibDelay ? d.vibDelay : 1) : 0;
    in.gateTimer = uint8_t((in.gateTimer & 0x7F) | (d.hardRestart ? 0 : 0x80));
    if (!(in.gateTimer & 0x3F)) in.gateTimer = uint8_t(in.gateTimer | 2);
    return Result::kOk;
}

}  // namespace designer
}  // namespace sidt
