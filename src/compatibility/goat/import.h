// SIDTracker -- GoatTracker import (PLAN.md §8 levels 1-3, docs/goattracker-mapping.md).
//
// Instruments and the four tables map 1:1. GoatTracker's per-channel order
// lists of single-channel patterns become SIDTracker's single order list of
// six-track patterns: the channels are laid side by side on one row clock,
// cut where any channel starts a pattern, identical cuts shared, channel
// transposes applied to the notes, and the loop reproduced exactly (intro
// = the latest channel intro, loop = the least common multiple of the
// channel loops). GoatTracker channels 1-3 go to SID1; SID2 stays empty.
// Anything that does not map exactly is counted in the Report.
#pragma once

#include <cstdint>

#include "compatibility/goat/gt_format.h"
#include "tracker/song/song.h"

namespace sidt {
namespace goat {

struct Report {
    static constexpr int kLines = 12;
    uint32_t commands = 0;          // pattern commands in the source (excluding 0)
    uint32_t direct = 0;            // mapped with the same meaning
    uint32_t approximated = 0;      // play slightly differently
    uint32_t unsupported = 0;       // dropped / ignored
    char line[kLines][72] = {};     // details, most important first
    int lines = 0;
    bool ok = false;
    void note(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    // "97% commands mapped directly." style percentage (100 when none).
    uint32_t directPercent() const { return commands ? direct * 100u / commands : 100u; }
};

// GoatTracker song (one subtune) -> `out` (out.patterns must be set; the
// song is cleared first). Returns false when it cannot fit (the report says
// why); `out` is then cleared.
bool importSong(const Song& gt, int subtune, sidt::Song& out, Report& report);

// GoatTracker instrument into instrument slot `slot` of `out`, its table
// rows appended to the free ends of `out`'s tables and its pointers
// relocated (readme §6.2).
bool importInstrument(const InsFile& ins, int slot, sidt::Song& out, Report& report);

// Note bytes.
uint8_t noteFromGt(uint8_t gtNote);   // -> SIDTracker note value
uint8_t noteToGt(uint8_t note);       // -> GoatTracker byte

}  // namespace goat
}  // namespace sidt
