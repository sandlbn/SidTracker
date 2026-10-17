// SIDTracker -- song data model (PLAN.md §7, §9, §14).
//
// Six logical tracks (1-3 -> SID1 voices 1-3, 4-6 -> SID2 voices 1-3), rows
// of NOTE | INSTRUMENT | COMMAND, GoatTracker-style instruments driven by
// shared wave / pulse / filter / speed tables, and one order list with
// transpose and repeat per entry. Plain data only: persistent storage goes
// through tracker/song/project_file (never the C++ layout, PLAN.md §15).
#pragma once

#include <cstdint>

#include "audio/effects.h"
#include "sid/tables.h"

namespace sidt {

constexpr int kChannels = 6;
constexpr int kChips = 2;
constexpr int kMaxRows = 128;
constexpr int kDefaultRows = 64;
constexpr int kPatternCount = 128;
constexpr int kInstrumentCount = 64;   // 1..63 usable; 0 = "no instrument"
constexpr int kTableLen = 255;         // rows 1..255 (0 = "no pointer")
constexpr int kOrderLen = 256;
constexpr int kNoteCount = 96;         // C-0 .. B-7
constexpr int kNameLen = 24;

namespace note {
constexpr uint8_t kEmpty = 0;
constexpr uint8_t kFirst = 1;          // C-0; note value n -> semitone n - 1
constexpr uint8_t kLast = kNoteCount;  // B-7
constexpr uint8_t kKeyOn = 0xFD;       // "+++" gate on
constexpr uint8_t kKeyOff = 0xFE;      // "===" gate off
inline bool isNote(uint8_t n) { return n >= kFirst && n <= kLast; }
}  // namespace note

// One row of one track. cmd 0..15 with an 8-bit parameter
// (tracker/commands/commands.h); cmd 0 = no command.
struct Cell {
    uint8_t note = note::kEmpty;
    uint8_t inst = 0;
    uint8_t cmd = 0;
    uint8_t param = 0;
    bool empty() const { return !note && !inst && !cmd && !param; }
};

// Perform automation lanes (PLAN.md §10, M7): the Perform page's live
// controls, recorded per pattern row. Values: wave 0 (song) or waveform bits
// 0x10..0x80; pulse width offset -0xFFF..0xFFF; cutoff offset -2047..2047;
// resonance -1 (song) or 0..15.
enum AutoLane : uint8_t {
    kAutoWave = 0,        // + track 0..5
    kAutoPw = 6,          // + track 0..5
    kAutoCutoff = 12,     // + chip 0..1
    kAutoRes = 14,        // + chip 0..1
    kAutoLanes = 16,
};
const char* autoLaneName(int lane);   // "PW 3", "CUT SID2"...

struct Pattern {
    uint8_t rows = kDefaultRows;       // 1..kMaxRows
    // Tracks whose (GoatTracker) pattern starts here, bit per track. A
    // native pattern starts on all tracks; imported songs, whose channels
    // change patterns at different rows, mark only those that do. The
    // player skips the pulse table on tick 0 of the row before (GT's order
    // list advance).
    uint8_t starts = 0x3F;
    Cell cells[kMaxRows][kChannels];
    // PCM drum column (M8): kit slot 1..16 triggered on the row, 0 = none.
    uint8_t drum[kMaxRows] = {};
    // Automation: bit per lane set on a row = the lane takes autoVal there.
    uint16_t autoSet[kMaxRows] = {};
    int16_t autoVal[kMaxRows][kAutoLanes] = {};
    void clear(uint8_t length = kDefaultRows);
    bool empty() const;
    void setAuto(int row, int lane, int16_t v) {
        autoSet[row] = uint16_t(autoSet[row] | (1u << lane));
        autoVal[row][lane] = v;
    }
    void clearAuto(uint16_t laneMask);   // all rows
    uint16_t autoLanes() const;          // lanes used anywhere
};

struct Instrument {
    char name[kNameLen + 1] = {};
    uint8_t ad = 0x09;                 // attack/decay register value
    uint8_t sr = 0x00;                 // sustain/release register value
    // Control value on the note's init tick: 01..FD written, FE gate off /
    // FF gate on with the waveform unchanged, 00 neither changed.
    uint8_t firstWave = 0x09;
    uint8_t wavePtr = 0;               // 1-based table rows, 0 = none
    uint8_t pulsePtr = 0;
    uint8_t filterPtr = 0;
    uint8_t vibParam = 0;              // speed table row for instrument vibrato
    uint8_t vibDelay = 0;              // ticks before instrument vibrato (0 = no vibrato)
    // GoatTracker HR/gate timer: bits 0..5 = ticks before the next row the
    // row is fetched and a new note gets gate off + hard restart (0: no
    // early fetch); 0x80 = no hard restart, 0x40 = no gate off (legato).
    uint8_t gateTimer = 2;
    bool empty() const;
    void clear(int index);
};

enum TableKind : uint8_t { kWaveTable, kPulseTable, kFilterTable, kSpeedTable, kTableKinds };

struct Table {
    uint8_t left[kTableLen] = {};
    uint8_t right[kTableLen] = {};
    // 1-based row access; row 0 or > kTableLen reads as 0.
    uint8_t l(int row) const { return row >= 1 && row <= kTableLen ? left[row - 1] : 0; }
    uint8_t r(int row) const { return row >= 1 && row <= kTableLen ? right[row - 1] : 0; }
    int used() const;                  // highest non-empty row, 0 = empty
};

struct OrderEntry {
    uint8_t pattern = 0;
    int8_t transpose = 0;              // semitones
    uint8_t repeat = 1;                // 1..16
};

enum class SidClock : uint8_t { kPal = 0, kNtsc = 1 };

struct SidConfig {
    sid::Model model[kChips] = {sid::Model::k8580, sid::Model::k8580};
    SidClock clock = SidClock::kPal;
    uint8_t balance = 50;              // 0 = mono, 100 = SID1 left / SID2 right
    uint32_t clockHz() const { return clock == SidClock::kNtsc ? sid::kNtscClockHz : sid::kPalClockHz; }
};

enum class SongMode : uint8_t { kAuthentic = 0, kExtended = 1 };

// Extended modulation (M8): LFOs and per-voice pan.
struct Lfo {
    enum Shape : uint8_t { kTri, kSine, kSquare, kSaw, kRandom, kShapes };
    // 0 off, 1..6 pulse width of track 1..6, 7..8 cutoff of SID1..2.
    uint8_t target = 0;
    uint8_t shape = kTri;
    uint8_t period = 32;   // ticks per cycle, 2..255
    uint8_t depth = 0;     // 0..100 (pulse width +-2000, cutoff +-1000 at 100)
    bool active() const { return target && depth; }
};
struct ModSettings {
    Lfo lfo[2];
    int8_t pan[kChannels] = {};   // -100 left .. 100 right; 0 = follow the SID balance
    bool any() const;
};
const char* lfoTargetName(uint8_t target);
const char* lfoShapeName(uint8_t shape);

struct Song {
    char name[kNameLen + 1] = {};
    char author[kNameLen + 1] = {};
    // Tick rate = bpm * 2 / 5 Hz (125 = 50 Hz PAL frame, 150 = 60 Hz).
    uint8_t bpm = 125;
    uint8_t speed = 6;                 // ticks per row
    uint8_t hrAD = 0x0F, hrSR = 0x00;  // ADSR written for the hard restart
    uint8_t orderLen = 1;
    uint8_t restartPos = 0;
    SongMode mode = SongMode::kAuthentic;
    SidConfig sid;
    OrderEntry order[kOrderLen];
    Instrument inst[kInstrumentCount];
    Table tables[kTableKinds];
    // Extended features (M8; not exported to GoatTracker).
    audio::FxSettings fx;
    uint8_t drumVolume = 80;           // PCM drums, 0..100 %
    ModSettings mod;
    // kPatternCount patterns, owned by the app (SDRAM on the device).
    Pattern* patterns = nullptr;

    // Empty song: one order entry (pattern 0), instrument 1 a default pulse
    // lead with its wave/pulse table rows. Keeps `patterns` and clears them.
    void clear();
    Pattern& pattern(int i) { return patterns[i & (kPatternCount - 1)]; }
    const Pattern& pattern(int i) const { return patterns[i & (kPatternCount - 1)]; }
    // Features that GoatTracker export would lose (PLAN.md §11): non-PAL/NTSC
    // tick rates, Extended mode. Returns a bit mask of ExtFeature.
    uint32_t extendedFeatures() const;
    // Automation lanes used by patterns in the order list.
    uint16_t automatedLanes() const;
    // True when a pattern in the order list uses the drum column.
    bool usesDrums() const;
};

enum ExtFeature : uint32_t {
    kExtTempo = 1u << 0,               // bpm not 125 or 150
    kExtMode = 1u << 1,                // song flagged Extended
    kExtAutomation = 1u << 2,          // Perform automation
    kExtEffects = 1u << 3,             // delay / chorus / reverb
    kExtDrums = 1u << 4,               // PCM drum column
    kExtModulation = 1u << 5,          // LFOs, per-voice pan
};

// Note value (1..96) -> "C-4" (3 chars + NUL); key on/off "+++"/"===";
// empty "---".
void noteName(uint8_t note, char out[4]);

}  // namespace sidt
