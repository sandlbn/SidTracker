# `.sid` export (M9)

File > F6 EXPORT SID writes `/SIDTracker/SID/NAME.sid`: a PSID file that
plays in SID players (libsidplayfp-based ones such as Phosphor, VICE's
VSID, JSIDPlay2) and on a C64 or with a SID cartridge. The first press
shows what will play; the second press writes the file.

## What is in the file

- **SIDTracker's own play routine** (`src/compatibility/sid/player.s`, MIT),
  a 6502 port of `src/tracker/playback/player.cpp`. It interprets the song
  data on the C64 every frame; it is not a register dump. GoatTracker's
  player is not used (DECISIONS.md 2).
- **The song** as compact data: wave / pulse / filter / speed tables,
  instruments, order list (pattern, transpose, repeat), and per pattern
  and track a stream of rows (empty rows run-length coded, identical tracks
  stored once).

| | |
|---|---|
| Format | PSID v2 (one SID) or v3 (two SIDs, the second at `$D420`) |
| Memory | `$1000`: player code and variables (3.3 KB), song area (5.2 KB), pattern streams; must end below `$D000` |
| Zero page | `$FB`-`$FE` |
| Calls | init `$1006`, play `$105D`, once per tick |
| Tempo | 125 bpm PAL / 150 bpm NTSC: frame interrupt; any other tempo: CIA 1 timer A set by init |
| SID models, clock | from Settings (per chip for dual-SID files) |
| CPU | about 1,500-2,000 cycles per frame for three tracks, 3,500 for six (6,000 peak); a PAL frame has 19,656 |

Tracks 4-6 make it a dual-SID file; a song that leaves them empty exports
for one SID.

## What does not play on a C64

The export check lists each one that the song uses; they are left out:
PCM drum column, effects (delay / chorus / reverb), LFOs and voice pan,
Perform automation, the Perform page's live overrides.

## Verification

`tools/oracle/sidcompare SONG.sidt|SONG.sng [--frames N] [--out F.sid]`
exports a song, runs the file's init and play routines on the oracle's
6502 emulator (`tools/oracle/cpu6502.h`) and compares all SID registers
after every frame with SIDTracker's player. Results (2026-10-16):

- DUALDEMO and all 14 example songs shipped with GoatTracker 2.77 (imported):
  0 differing frames over 30,000 frames (10 minutes) each.
- `tests/psid_test.cpp`: 40 random songs (every command, table row kind,
  jumps, transposes, repeats, odd tempos, one and two SIDs), 1,200 frames
  each, 0 differing frames. Deliberately broken player builds fail it.

Rebuild the play routine after editing `player.s` with `make sidplayer`
(the assembler is `tools/asm6502`, our own; `make check` fails when
`player_bin.h` is stale).
