// SIDTracker -- SID constants and generated tables (APPROACH.md §3).
//
// The combined-waveform tables and filter curves are our own models,
// generated at boot (main loop) from a few parameters; no measured tables
// from other emulators are used (DECISIONS.md 2).
#pragma once

#include <cstdint>

namespace sidt {
namespace sid {

enum class Model : uint8_t { k6581 = 0, k8580 = 1 };
constexpr int kModelCount = 2;

constexpr uint32_t kPalClockHz = 985248;     // C64 PAL system clock
constexpr uint32_t kNtscClockHz = 1022727;   // C64 NTSC system clock

// ADSR rate counter periods in SID clocks for the 16 rate values (attack;
// decay and release use the same periods, stepped through the exponential
// counter). Datasheet attack times 2 ms .. 8 s = 255 steps x period.
extern const uint16_t kRatePeriod[16];

// Exponential counter period for an envelope level (decay/release only):
// 1 above 0x5D, then 2, 4, 8, 16, 30 below 0x5D, 0x36, 0x1A, 0x0E, 0x06.
inline uint8_t expPeriod(uint8_t level) {
    if (level > 0x5D) return 1;
    if (level > 0x36) return 2;
    if (level > 0x1A) return 4;
    if (level > 0x0E) return 8;
    if (level > 0x06) return 16;
    if (level > 0x00) return 30;
    return 1;
}

// Combined waveforms (control nibble 3, 5, 6, 7): output top 8 bits indexed
// by the 12-bit sawtooth value (accumulator >> 12). For the pulse
// combinations the table is the "pulse high" output; pulse low gives 0.
enum Combo : uint8_t { kComboTS = 0, kComboPT = 1, kComboPS = 2, kComboPST = 3, kComboCount = 4 };
const uint8_t* comboTable(Model m, Combo c);

// Filter: TPT state-variable coefficient g = tan(pi * fc / fs) for each
// 11-bit cutoff register value, per model, for the current sample rate.
const float* cutoffG(Model m);
// Cutoff curve in Hz for an 11-bit FC value (8580 near linear, 6581 an
// exponential-ish S curve with a ~220 Hz floor; parameterised, "8580
// accurate, 6581 plausible" for 0.1, APPROACH.md risks).
float cutoffHz(Model m, uint16_t fc);
// Damping k = 1 / Q for the 4-bit resonance value.
float resonanceK(Model m, uint8_t res);

// Builds the tables above for `sampleRateMilliHz`. Main loop only (uses
// tanf / expf); call once at boot and again if the rate changes.
void initTables(uint32_t sampleRateMilliHz);
bool tablesReady();

}  // namespace sid
}  // namespace sidt
