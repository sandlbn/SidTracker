// SIDTracker -- song data model (song.h).
#include "tracker/song/song.h"

#include <cstdio>
#include <cstring>

namespace sidt {

void Pattern::clear(uint8_t length) {
    rows = length >= 1 && length <= kMaxRows ? length : kDefaultRows;
    starts = 0x3F;
    for (auto& row : cells)
        for (Cell& c : row) c = Cell{};
    for (int r = 0; r < kMaxRows; ++r) {
        drum[r] = 0;
        autoSet[r] = 0;
        for (int16_t& v : autoVal[r]) v = 0;
    }
}

void Pattern::clearAuto(uint16_t mask) {
    for (int r = 0; r < kMaxRows; ++r) autoSet[r] = uint16_t(autoSet[r] & ~mask);
}

uint16_t Pattern::autoLanes() const {
    uint16_t m = 0;
    for (int r = 0; r < rows; ++r) m = uint16_t(m | autoSet[r]);
    return m;
}

const char* autoLaneName(int lane) {
    static const char* const kNames[kAutoLanes] = {
        "WAVE 1", "WAVE 2", "WAVE 3", "WAVE 4", "WAVE 5", "WAVE 6", "PW 1", "PW 2",
        "PW 3", "PW 4", "PW 5", "PW 6", "CUT SID1", "CUT SID2", "RES SID1", "RES SID2"};
    return lane >= 0 && lane < kAutoLanes ? kNames[lane] : "?";
}

bool Pattern::empty() const {
    for (int r = 0; r < rows; ++r) {
        if (autoSet[r] || drum[r]) return false;
        for (const Cell& c : cells[r])
            if (!c.empty()) return false;
    }
    return true;
}

bool Instrument::empty() const {
    Instrument d;
    return ad == d.ad && sr == d.sr && firstWave == d.firstWave && !wavePtr && !pulsePtr && !filterPtr &&
           !vibParam && !vibDelay && gateTimer == d.gateTimer;
}

void Instrument::clear(int index) {
    *this = Instrument{};
    std::snprintf(name, sizeof name, "INSTR %02X", index);
}

int Table::used() const {
    for (int i = kTableLen; i >= 1; --i)
        if (left[i - 1] || right[i - 1]) return i;
    return 0;
}

void Song::clear() {
    Pattern* keep = patterns;
    *this = Song{};
    patterns = keep;
    std::strcpy(name, "UNTITLED");
    for (int i = 1; i < kInstrumentCount; ++i) inst[i].clear(i);
    // Instrument 1: pulse lead. Wave table row 1: pulse + gate at the note's pitch,
    // row 2: stop. Pulse table row 1: width 0x400, rows 2-3: sweep +0x20
    // then -0x20 per tick for 0x40 ticks each, row 4: jump to 2.
    Instrument& lead = inst[1];
    std::strcpy(lead.name, "PULSE LEAD");
    lead.ad = 0x09;
    lead.sr = 0xA9;
    lead.firstWave = 0x09;
    lead.wavePtr = 1;
    lead.pulsePtr = 1;
    Table& w = tables[kWaveTable];
    w.left[0] = 0x41; w.right[0] = 0x00;
    w.left[1] = 0xFF; w.right[1] = 0x00;
    Table& p = tables[kPulseTable];
    p.left[0] = 0x84; p.right[0] = 0x00;
    p.left[1] = 0x40; p.right[1] = 0x20;
    p.left[2] = 0x40; p.right[2] = 0xE0;
    p.left[3] = 0xFF; p.right[3] = 0x02;
    if (patterns)
        for (int i = 0; i < kPatternCount; ++i) patterns[i].clear();
}

uint32_t Song::extendedFeatures() const {
    uint32_t f = 0;
    if (bpm != 125 && bpm != 150) f |= kExtTempo;
    if (mode == SongMode::kExtended) f |= kExtMode;
    if (automatedLanes()) f |= kExtAutomation;
    if (fx.any()) f |= kExtEffects;
    if (usesDrums()) f |= kExtDrums;
    if (mod.any()) f |= kExtModulation;
    return f;
}

bool ModSettings::any() const {
    if (lfo[0].active() || lfo[1].active()) return true;
    for (int8_t p : pan)
        if (p) return true;
    return false;
}

const char* lfoTargetName(uint8_t t) {
    static const char* const kNames[9] = {"OFF", "PW 1", "PW 2", "PW 3", "PW 4", "PW 5", "PW 6", "CUT SID1", "CUT SID2"};
    return t < 9 ? kNames[t] : "?";
}

const char* lfoShapeName(uint8_t s) {
    static const char* const kNames[Lfo::kShapes] = {"TRIANGLE", "SINE", "SQUARE", "SAW", "RANDOM"};
    return s < Lfo::kShapes ? kNames[s] : "?";
}

bool Song::usesDrums() const {
    if (!patterns) return false;
    for (int i = 0; i < orderLen; ++i) {
        const Pattern& p = pattern(order[i].pattern);
        for (int r = 0; r < p.rows; ++r)
            if (p.drum[r]) return true;
    }
    return false;
}

uint16_t Song::automatedLanes() const {
    if (!patterns) return 0;
    uint16_t m = 0;
    bool seen[kPatternCount] = {};
    for (int i = 0; i < orderLen; ++i) {
        const int p = order[i].pattern & (kPatternCount - 1);
        if (seen[p]) continue;
        seen[p] = true;
        m = uint16_t(m | pattern(p).autoLanes());
    }
    return m;
}

void noteName(uint8_t n, char out[4]) {
    static const char kNames[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
    if (note::isNote(n)) {
        const int s = n - 1;
        out[0] = kNames[(s % 12) * 2];
        out[1] = kNames[(s % 12) * 2 + 1];
        out[2] = char('0' + s / 12);
    } else {
        const char c = n == note::kKeyOff ? '=' : (n == note::kKeyOn ? '+' : '-');
        out[0] = out[1] = out[2] = c;
    }
    out[3] = 0;
}

}  // namespace sidt
