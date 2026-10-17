# Blast Grid 64 import

`make blastgrid` (needs `~/Projects/BlastGridC64`, or `GAME=DIR`) converts
the game's songs into SIDTracker projects in `build/blastgrid/sidt/`; the
checked-in copies are in `examples/blastgrid/`.

1. `tools/blastgrid/bg2text.py` imports the game's own parsers read-only
   (`tools/mk_music.py`, `tools/mk_psgmusic.py`, `sid/tools/sidlib.py`; no
   file in the game repository is written) and writes one song text per song.
2. `tools/text2sidt.cpp` builds the project with SIDTracker's song model and
   project writer (a generic song-text importer, format in its header).

Mapping:

| Blast Grid | SIDTracker |
|---|---|
| section | pattern (one per section, reused by the order list) |
| `order` with transposes, `|` loop point | order list with transposes, restart position; songs without a loop end on a silent pattern |
| C64 arrangement voices 0-5 (`sid/data/sid_arrange.txt`) | tracks 1-6 (lead, bass, drums on SID1; harmony on SID2) |
| `D4` / `.` / `^D4` / `-` | note / key off / tie (3 00) / continue; a section opening without a note keys off |
| `D4:037` (driver arpeggio) | an arp variant of the instrument: its wave program cycles 0 / 3 / 7 at the channel's arp speed |
| `@inst`, `inst=` overrides | instrument column |
| echo channel (`echo=lead:3`) | the lead's notes three rows later on its voice |
| SID instrument: wave, PW, AD / SR, `pwspd`, `vib`, `len`, hard restart | first wave + wave program, pulse program (bouncing $100-$EFF), vibrato speed row (note-independent depth), gate-off row after `len`, gate timer 3 with hard restart AD / SR 00 |
| drum programs, drum map (`sid_instruments.txt`) | one instrument per drum and pitch; steps at fixed pitches; the highest-priority hit of a row wins, as in the game |
| `tempo 7` / `tempo 7 6` | speed 7 / funk tempo (command E, speed-table row 7 6) |
| 59.52 Hz tick | 149 bpm (59.6 Hz; the `.sid` export uses the CIA timer) |

Check: `tools/blastgrid/compare_orig.py ORIGINAL.sid build/blastgrid/sid`
runs the game's own `blastgrid64_2sid.sid` and the exported imports on the
6502 emulator and compares the note each SID 1 voice sounds per frame.
2026-10-16: lead and bass match 100 % in all 13 songs at zero offset (a
deliberately mismatched pair scores about 6 %). Notes are one tick later
than the game's driver (SIDTracker, like GoatTracker, starts a note's
tables on the tick after the note), and the game's vibrato / PWM update
every second tick, so they are close, not identical.
