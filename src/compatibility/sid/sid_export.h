// SIDTracker -- .sid export (PLAN.md M9): a PSID file with SIDTracker's
// own C64 play routine (player.s, assembled into player_bin.h) and the song
// as compact data, so it plays in any SID player and on a C64. Dual-SID
// songs use a second SID at $D420 (PSID v3).
//
// What plays: everything the song player does (notes, instruments, wave /
// pulse / filter / speed tables, commands, order list with transpose and
// repeat, hard restart). Not on a C64: PCM drums, effects, LFOs, voice pan,
// Perform automation -- checkExport() lists them.
#pragma once

#include <cstdint>

#include "compatibility/goat/import.h"   // goat::Report
#include "tracker/song/song.h"

namespace sidt {
namespace psid {

constexpr uint16_t kLoadAddress = 0x1000;
constexpr uint32_t kMaxFile = 0x7C + 2 + (0xD000 - kLoadAddress);   // must end below the I/O area
// Work memory exportSid() / checkExport() need (the file plus a table).
constexpr uint32_t kTableOffset = (kMaxFile + 7) & ~7u;
constexpr uint32_t kWorkBytes = kTableOffset + kPatternCount * kChannels * 8;

struct ExportCheck {
    bool ok = true;
    bool dual = false;        // tracks 4-6 used: second SID at $D420
    bool cia = false;         // tempo other than the machine's frame rate
    uint32_t bytes = 0;       // file size
    goat::Report report;      // "! ..." warnings first, then facts
};

// `work`: kWorkBytes of scratch memory (SDRAM on the device).
ExportCheck checkExport(const Song& s, uint8_t* work, uint32_t workBytes);
// Writes the .sid at the start of `out` (`cap` >= kWorkBytes). Returns the
// size, 0 when the song does not fit (the report says why).
uint32_t exportSid(const Song& s, uint8_t* out, uint32_t cap, goat::Report& report);

}  // namespace psid
}  // namespace sidt
