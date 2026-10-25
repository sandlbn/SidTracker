// SIDTracker -- table housekeeping (tables.h).
#include "tracker/song/tables.h"

#include <cstdio>
#include <cstring>

namespace sidt {
namespace tables {

namespace {

// Pattern commands whose parameter is a row of table k.
bool cmdRefers(TableKind k, uint8_t cmd, uint8_t param) {
    if (!param) return false;
    switch (k) {
        case kWaveTable: return cmd == 0x8;
        case kPulseTable: return cmd == 0x9;
        case kFilterTable: return cmd == 0xA;
        default: return (cmd >= 0x1 && cmd <= 0x4) || cmd == 0xE;
    }
}

// Wave-table command rows (left F0-FE: command left & 0F, data right).
bool waveCmdRefers(TableKind k, uint8_t left, uint8_t right) {
    if (left < 0xF0 || left == 0xFF || !right) return false;
    return cmdRefers(k, uint8_t(left & 0x0F), right);
}

uint8_t* instPointer(Instrument& in, TableKind k) {
    switch (k) {
        case kWaveTable: return &in.wavePtr;
        case kPulseTable: return &in.pulsePtr;
        case kFilterTable: return &in.filterPtr;
        default: return &in.vibParam;
    }
}

// Calls f(ref) for every reference to a row of table k. `jumps`: also the
// FF xx rows inside the table (the speed table has none: its FF xx rows are
// note-independent speeds).
template <typename F>
void forEachRef(Song& s, TableKind k, F f, bool jumps = true) {
    for (int i = 1; i < kInstrumentCount; ++i) {
        uint8_t* p = instPointer(s.inst[i], k);
        if (*p) f(*p);
    }
    Table& t = s.tables[k];
    if (jumps && k != kSpeedTable)
        for (int r = 0; r < kTableLen; ++r)
            if (t.left[r] == 0xFF && t.right[r]) f(t.right[r]);
    Table& w = s.tables[kWaveTable];
    for (int r = 0; r < kTableLen; ++r)
        if (waveCmdRefers(k, w.left[r], w.right[r])) f(w.right[r]);
    if (!s.patterns) return;
    for (int q = 0; q < kPatternCount; ++q) {
        Pattern& pt = s.pattern(q);
        for (int r = 0; r < pt.rows; ++r)
            for (Cell& c : pt.cells[r])
                if (cmdRefers(k, uint8_t(c.cmd & 15), c.param)) f(c.param);
    }
}

}  // namespace

bool insertRow(Song& s, TableKind k, int row) {
    Table& t = s.tables[k];
    if (row < 1 || row > kTableLen || t.l(kTableLen) || t.r(kTableLen)) return false;
    for (int r = kTableLen - 1; r >= row; --r) {   // 0-based: row-1 .. 253 -> +1
        t.left[r] = t.left[r - 1];
        t.right[r] = t.right[r - 1];
    }
    t.left[row - 1] = t.right[row - 1] = 0;
    forEachRef(s, k, [row](uint8_t& ref) {
        if (ref >= row && ref < kTableLen) ++ref;
    });
    return true;
}

bool deleteRow(Song& s, TableKind k, int row) {
    Table& t = s.tables[k];
    if (row < 1 || row > kTableLen) return false;
    for (int r = row - 1; r < kTableLen - 1; ++r) {
        t.left[r] = t.left[r + 1];
        t.right[r] = t.right[r + 1];
    }
    t.left[kTableLen - 1] = t.right[kTableLen - 1] = 0;
    forEachRef(s, k, [row](uint8_t& ref) {
        if (ref > row) --ref;
    });
    return true;
}

int describeUsers(const Song& cs, TableKind k, int row, char* out, int cap) {
    Song& s = const_cast<Song&>(cs);   // forEachRef is not used here; plain reads
    int n = 0, len = 0;
    out[0] = 0;
    auto add = [&](const char* what, unsigned v) {
        if (len < cap - 1) {
            const int w = std::snprintf(out + len, size_t(cap - len), "%s%s %02X", n ? ", " : "", what, v);
            len = w > 0 ? (len + w < cap - 1 ? len + w : cap - 1) : len;
        }
        ++n;
    };
    for (int i = 1; i < kInstrumentCount; ++i)
        if (*instPointer(s.inst[i], k) == row) add("INS", unsigned(i));
    const Table& t = s.tables[k];
    if (k != kSpeedTable)
        for (int r = 1; r <= kTableLen; ++r)
            if (t.l(r) == 0xFF && t.r(r) == row) add("jump from", unsigned(r));
    const Table& w = s.tables[kWaveTable];
    for (int r = 1; r <= kTableLen; ++r)
        if (waveCmdRefers(k, w.l(r), w.r(r)) && w.r(r) == row) add("wave cmd", unsigned(r));
    if (s.patterns)
        for (int q = 0; q < kPatternCount; ++q) {
            const Pattern& pt = s.pattern(q);
            bool hit = false;
            for (int r = 0; r < pt.rows && !hit; ++r)
                for (const Cell& c : pt.cells[r])
                    if (cmdRefers(k, uint8_t(c.cmd & 15), c.param) && c.param == row) hit = true;
            if (hit) add("cmd in PAT", unsigned(q));
        }
    if (!n && k != kSpeedTable) {
        // Not a program start: is it inside one? Follow each instrument's program.
        for (int i = 1; i < kInstrumentCount && !n; ++i) {
            int r = *instPointer(s.inst[i], k);
            bool seen[kTableLen + 1] = {};
            while (r >= 1 && r <= kTableLen && !seen[r]) {
                seen[r] = true;
                if (r == row) {
                    char what[40];
                    std::snprintf(what, sizeof what, "in the program of INS %02X from", unsigned(i));
                    add(what, unsigned(*instPointer(s.inst[i], k)));
                    break;
                }
                r = t.l(r) == 0xFF ? t.r(r) : r + 1;
            }
        }
    }
    return n;
}

int pack(Song& s, TableKind k) {
    bool reached[kTableLen + 1] = {};
    const Table& t = s.tables[k];
    const bool programs = k != kSpeedTable;   // speed rows stand alone
    forEachRef(s, k, [&](uint8_t& ref) {
        int r = ref;
        for (int guard = 0; r >= 1 && r <= kTableLen && !reached[r] && guard < kTableLen; ++guard) {
            reached[r] = true;
            if (!programs) break;
            if (t.l(r) == 0xFF) {
                r = t.r(r);   // jump (0: stop)
            } else {
                ++r;
            }
        }
    }, false);   // entries only: a jump row reaches its target only if it is reached itself
    int removed = 0;
    for (int r = t.used(); r >= 1; --r)
        if (!reached[r]) {
            deleteRow(s, k, r);
            ++removed;
        }
    return removed;
}

}  // namespace tables
}  // namespace sidt
