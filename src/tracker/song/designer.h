// SIDTracker -- sound designer (instrument DESIGN page): synth-style
// parameters that write an instrument's wave, pulse and filter programs.
//
// Each program has a fixed shape, so the designer can read its settings back
// from the tables (decode) and rewrite them in place on every change, as
// long as no other instrument points into those rows; otherwise it writes a
// fresh block after the used rows. Programs (rows, jumps relative):
//   wave    transient row, main wave row, FF 00
//   pulse   set width, ticks +speed, ticks -speed, FF -> row 2
//   filter  mode / resonance / voice, cutoff, sweep A, sweep B, FF (00 or -> row 3)
// Vibrato uses a shared speed-table row (found or added).
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {
namespace designer {

enum Transient : uint8_t { kNone, kClick, kOctave, kFifth, kTransients };
enum FilterType : uint8_t { kFilterOff, kLowPass, kBandPass, kHighPass, kNotch, kFilterTypes };
enum Sweep : uint8_t { kSweepOff, kSweepDown, kSweepUp, kWobble, kSweeps };

struct Design {
    uint8_t wave = 0x40;          // waveform bits 0x10..0x80 (combinations allowed)
    uint8_t transient = kNone;
    uint8_t attack = 0, decay = 9, sustain = 10, release = 9;   // 0..15
    uint16_t pw = 0x800;          // 0..0xFFF
    uint8_t pwmSpeed = 0;         // 0..0x7F per tick
    uint8_t pwmTicks = 0x20;      // 1..0x7F per direction
    uint8_t filter = kFilterOff;
    uint8_t cutoff = 0x80;        // high byte
    uint8_t resonance = 8;        // 0..15
    uint8_t sweep = kSweepOff;
    uint8_t sweepSpeed = 2;       // 1..0x7F per tick
    uint8_t sweepTicks = 0x30;    // 1..0x7F
    uint8_t vibDepth = 0;         // 0 = off
    uint8_t vibSpeed = 4;         // 1..0x7F
    uint8_t vibDelay = 8;         // ticks before the vibrato
    bool hardRestart = true;
};

const char* waveName(uint8_t wave);       // "PULSE", "SAW+TRI", ...
const char* transientName(int t);
const char* filterName(int f);
const char* sweepName(int s);
// Waveform choices in the order the jog wheel steps through them.
int waveChoices(const uint8_t** out);

// The design of instrument `slot`: decoded from its programs where they
// have the designer's shape, the instrument's own values otherwise.
Design decode(const Song& s, int slot);

// True when the instrument's wave and pulse programs already have the
// designer's shape (editing then only rewrites them in place).
bool isDesigned(const Song& s, int slot);

enum class Result : uint8_t { kOk, kTableFull };
// Writes the design into instrument `slot`; `voice` (0..2) is the SID voice
// the filter routes. A full table leaves the song unchanged.
Result apply(Song& s, int slot, const Design& d, int voice);

}  // namespace designer
}  // namespace sidt
