// SIDTracker -- sound library and arpeggiator insert (instrument SOUNDS page).
//
// Presets are SIDTracker's own instruments built on classic C64 techniques
// (hard-restart basses, chord arpeggios, noise + pulse drums, pulse and
// filter sweeps, ring-modulated bells): parameters plus short wave / pulse
// / filter programs. Loading one copies it into an instrument slot and
// appends its table rows after the rows the song uses (identical rows
// already there are reused); jumps inside a program are relocated.
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {
namespace library {

enum Category : uint8_t { kBass, kLead, kChord, kDrum, kPad, kFx, kKeys, kCategories };
const char* categoryName(int c);

struct Row {
    uint8_t l, r;
};

// Jumps (left FF, right != 0) in a preset's programs are 1-based rows of
// that program.
struct Preset {
    const char* name;
    Category category;
    const char* text;          // one-line description
    uint8_t ad, sr, firstWave, gateTimer;
    uint8_t vibSpeed, vibDepth, vibDelay;   // vibrato speed-table row values; delay 0 = none
    Row wave[16];
    Row pulse[8];
    Row filter[8];             // route bits in the first row are set for the voice on load
};

int presetCount();
const Preset& preset(int i);
// Index of the first preset in a category, and how many there are.
int categoryFirst(int c);
int categoryCount(int c);

enum class Result : uint8_t { kOk, kTableFull };
const char* resultText(Result r);

// Loads preset `p` into instrument `slot` (1..63); `voice` (0..2) is the
// SID voice its filter routes. The slot keeps nothing of its old contents
// except table rows other instruments may still use.
Result load(Song& s, int slot, const Preset& p, int voice);

// Arpeggiator insert: a looping wave program for instrument `slot`.
enum Chord : uint8_t { kMajor, kMinor, kSeventh, kMajor7, kMinor7, kSus2, kSus4, kDim, kAug, kPower, kOctave,
                       kSixth, kMinor9, kChords };
enum Order : uint8_t { kUp, kDown, kUpDown, kUpOctave, kOrders };
enum ArpWave : uint8_t { kPulse, kSaw, kTriangle, kSawTri, kArpWaves };
const char* chordName(int c);
const char* orderName(int o);
const char* arpWaveName(int w);
struct Arp {
    uint8_t chord = kMajor, order = kUp, ticks = 1, wave = kPulse;   // ticks per note 1..4
};
// Number of wave table rows the arp needs, and its note offsets (for the UI).
int arpNotes(const Arp& a, int8_t* out, int cap);
// The wave program an arp writes (jump relative to its first row, as in
// presets); returns the row count.
int arpProgram(const Arp& a, Row* out, int cap);
// Points the instrument's wave program at the new rows; a pulse arp on an
// instrument without a pulse program also gets one (pulse width $800).
Result insertArp(Song& s, int slot, const Arp& a);

}  // namespace library
}  // namespace sidt
