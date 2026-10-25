// SIDTracker -- table housekeeping (Instrument screen table pages): insert
// and delete rows with every reference fixed up, list a row's users, drop
// rows nothing reaches. Pure functions over a Song.
//
// References to row r of table k (1-based, 0 = none):
//   instrument pointers   wave / pulse / filter pointer; speed: vibrato row
//   jumps                 FF xx rows inside the same table
//   pattern commands      8xx wave, 9xx pulse, Axx filter; speed table:
//                         1xx 2xx 3xx 4xx Exx (3xx / 4xx 00 = legato / off)
//   wave-table commands   F8 / F9 / FA pointer rows; F1-F4 / FE speed rows
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {
namespace tables {

// Inserts an empty row before `row` (1..255): rows below move down one and
// every reference to them follows. False (nothing changed) when the last
// row is in use.
bool insertRow(Song& s, TableKind k, int row);
// Deletes `row`: rows below move up one; references to the deleted row now
// point at the row that took its place.
bool deleteRow(Song& s, TableKind k, int row);
// What refers to `row`: "INS 02, INS 05, 8xx in PAT 04, jump from 0A".
// Returns the number of references (0: unused).
int describeUsers(const Song& s, TableKind k, int row, char* out, int cap);
// Removes rows that no instrument, command or jump can reach (programs are
// followed to their FF row). Returns the number of rows removed.
int pack(Song& s, TableKind k);

}  // namespace tables
}  // namespace sidt
