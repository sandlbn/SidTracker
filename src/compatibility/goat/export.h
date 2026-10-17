// SIDTracker -- GoatTracker export (PLAN.md §16, docs/goattracker-mapping.md).
//
// checkExport() lists what will and will not survive (the export screen);
// exportSong() writes tracks 1-3 (SID1) as GoatTracker's three channels:
// every SIDTracker pattern becomes one single-channel pattern per track
// (identical ones shared), the order list is written per channel with
// transpose and repeat commands. Instruments and tables are copied 1:1.
#pragma once

#include <cstdint>

#include "compatibility/goat/gt_format.h"
#include "compatibility/goat/import.h"
#include "tracker/song/song.h"

namespace sidt {
namespace goat {

// What the export keeps / changes. `ok` = the song can be written.
struct ExportCheck {
    bool ok = true;
    bool notes = true, instruments = true, tables = true, orders = true;
    Report report;   // "! ..." warnings first, then counts
};

ExportCheck checkExport(const sidt::Song& s);
// Fills `gt` (large: allocate it in scratch memory). False when it does
// not fit GoatTracker's limits (the report says why).
bool exportSong(const sidt::Song& s, Song& gt, Report& report);
// Instrument `slot` with the table rows it reaches (readme §6.2: a partial
// table snapshot, pointers relative to it).
bool exportInstrument(const sidt::Song& s, int slot, InsFile& out);

}  // namespace goat
}  // namespace sidt
