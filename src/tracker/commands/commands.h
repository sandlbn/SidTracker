// SIDTracker -- pattern commands (PLAN.md §7; semantics in docs/commands.md).
//
// One command nibble with an 8-bit parameter, modelled on the GoatTracker
// command set so songs map structurally (PLAN.md §8). The exact GoatTracker
// behaviour is verified in the M5 research task (docs/goattracker-format.md);
// until then these are SIDTracker's own definitions.
#pragma once

#include <cstdint>

namespace sidt {
namespace cmd {

enum : uint8_t {
    kNone = 0x0,
    kPortaUp = 0x1,        // speed table row: 16-bit freq units per tick
    kPortaDown = 0x2,
    kTonePorta = 0x3,      // speed table row; 00 = legato (no retrigger)
    kVibrato = 0x4,        // speed table row: left = ticks per half cycle, right = depth
    kSetAD = 0x5,
    kSetSR = 0x6,
    kSetWave = 0x7,        // control register bits (gate kept)
    kWavePtr = 0x8,
    kPulsePtr = 0x9,
    kFilterPtr = 0xA,
    kFilterCtrl = 0xB,     // RES/FILT: resonance << 4 | voice routing
    kCutoff = 0xC,         // cutoff high byte
    kVolume = 0xD,         // master volume (low nibble)
    kFunkTempo = 0xE,      // speed table row: left / right speeds alternate per row
    kTempo = 0xF,          // ticks per row (01..7F)
    kCount = 16,
};

// Short name for the Pattern screen help line, e.g. "PORTA UP".
const char* name(uint8_t c);
// Parameter meaning, e.g. "speed tbl row".
const char* paramHelp(uint8_t c);
// True if the parameter is a speed table row (the editor offers a jump).
bool usesSpeedTable(uint8_t c);

}  // namespace cmd
}  // namespace sidt
