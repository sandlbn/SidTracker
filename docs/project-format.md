# Native project file: `.sidt`

Format version **1**, written by `src/tracker/song/project_file.cpp`, tested
by `tests/project_test.cpp` (round trip, unknown chunks, damaged files).
Projects live on the card in `/SIDTracker/Projects/<NAME>.sidt`; the
workspace is autosaved to `/SIDTracker/autosave.sidt` (every 2 minutes when
changed and stopped, on power-off, and after every save) and restored at
boot. Saves go to `<file>.tmp` first and replace the old file only after a
complete write.

Rules (PLAN.md §15): every field is written byte by byte, little-endian;
nothing depends on C++ structure layout. Unknown chunks are skipped, and
records carry field counts, so later versions add data without breaking
older readers. A reader refuses files with a newer format version.

## Layout

```
"SIDT"  u16 version  u16 flags(0)
chunk*  : tag[4]  u32 length  payload[length]
"END "  u32 0
```

Strings are `u8 length` + bytes (at most 24).

| Tag | Count | Payload |
|---|---|---|
| `META` | 1 | name str, author str, bpm u8, speed u8, hard-restart AD u8, SR u8, mode u8 (0 Authentic, 1 Extended), restart position u8 |
| `SIDC` | 1 | SID1 model u8 (0 6581, 1 8580), SID2 model u8, clock u8 (0 PAL, 1 NTSC), balance u8 (0–100) |
| `ORDR` | 1 | count u8, then per entry: pattern u8, transpose s8, repeat u8 |
| `PATT` | per stored pattern | index u8, rows u8 (1–128), then rows × 6 cells × (note, instrument, command, parameter), then the track start mask u8 (added in 0.2; absent = all tracks) |
| `INST` | per non-default instrument | index u8 (1–63), field count u8 (9), AD, SR, first wave, wave ptr, pulse ptr, filter ptr, vibrato row, vibrato delay, gate timer (GoatTracker HR / gate timer byte), name str |
| `TABL` | per non-empty table | kind u8 (0 wave, 1 pulse, 2 filter, 3 speed), used rows u8, left bytes, right bytes |
| `END ` | 1 | – |

Stored patterns: every pattern referenced by the order list, plus any other
pattern that is not empty or has a non-default length.

`AUTO` (0.3, per pattern with automation): pattern u8, rows u8, then per
automated row: row u8, lane mask u16, the set lanes' values s16 in lane
order; `FF` ends the list. Lanes: 0-5 waveform override per track, 6-11
pulse-width offset, 12-13 cutoff offset per chip, 14-15 resonance per chip.

`DRUM` (0.3, M8; once, when any pattern has drums): per pattern with drums:
pattern u8, then (row u8, slot u8) pairs, `FF` ends the pattern; a pattern
byte `FF` ends the chunk.

`EXTN` (0.3, M8; once): field count u8 (24), then u8 fields: delay mix,
delay ticks, delay feedback, chorus mix, rate, depth, reverb mix, size,
damping, drum volume, LFO 1 target, shape, period, depth, LFO 2 the same,
pan s8 × 6. A reader takes the fields it knows and keeps defaults for the
rest.

Reserved: `SMPL` (kit samples inside the project; today the kit is the
files in `/SIDTracker/Kit`). Readers skip unknown chunks.
