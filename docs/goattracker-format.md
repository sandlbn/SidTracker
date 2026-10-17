# GoatTracker 2 file formats and playback (.sng, .ins)

What SIDTracker relies on when it reads, writes and plays GoatTracker 2
songs. Sources, as DECISIONS.md 3 requires:
- GoatTracker 2.77's documentation (`readme.txt`, cited as "readme §n").
- Files produced by GoatTracker itself: the 14 example songs and 4 instruments shipped with 2.77.
- Experiments with GoatTracker's own packer/relocator `gt2reloc`, run out of process (see "Oracle" below).

No GoatTracker source code was read, copied or linked.

Source of the documentation and fixtures: the Arch Linux package
`goattracker 2.77-2` (`/usr/share/doc/goattracker/`). The example songs are
GoatTracker's, not ours, so they live in the git-ignored
`fixtures/private/goattracker/`; tests skip them when absent.

## .sng (GTS5)

readme §6.1, checked byte for byte: `tests/goat_test.cpp` reads all 14
example songs and writes them back byte-identical
(`goattracker_produced_songs_when_present`).

| Part | Layout |
|---|---|
| Header | `GTS5`, name[32], author[32], copyright[32] (zero padded), subtune count u8 |
| Order lists | per subtune, channels 1-3: length u8, then that many bytes, then the restart position u8 |
| Instruments | count u8 (instrument 0 not stored), then 25 bytes each: AD, SR, wave ptr, pulse ptr, filter ptr, vibrato param, vibrato delay, gate timer, first-frame wave, name[16] |
| Tables | wave, pulse, filter, speed: row count u8, left bytes, right bytes |
| Patterns | count u8, then per pattern: row count u8, rows x (note, instrument, command, data) |

Settled with the files (the readme is ambiguous here):
- The order-list length byte counts the bytes up to and including the `FF` (RST) mark; the restart position byte follows. The readme says "n, not counting restart pos" and "n+1 bytes".
- The pattern row count includes the `FF` end-mark row (every pattern's last row has note byte `FF`).

Bytes:
- Order lists: `00`–`CF` pattern, `D0`–`DF` repeat, `E0`–`FE` transpose = value − `F0` (−16..+14), `FF` end.
- Notes: `60`–`BC` C-0..G#7, `BD` rest, `BE` key off, `BF` key on.

Older formats (GTS2..GTS4, GTI2..GTI4) are refused with "resave it in
GoatTracker 2.59 or later". Their conversions (3 tables, pulse speed) are
not documented well enough to reproduce. The four example `.ins` files
are GTI2.

## .ins (GTI5)

readme §6.2: `GTI5`, the 9 instrument bytes, name[16], then the four
tables as above. The tables are the rows the instrument uses. On import they
are appended to the song's tables and every pointer and jump is relocated.
On export the reachable rows are written: from the pointers, following
jumps, plus the speed rows of the vibrato and of wave-table commands.

## Playback (readme §3, measured where the readme is silent)

`src/tracker/playback/player.cpp` implements this model. `docs/commands.md`
has the user-facing semantics.

Per row of `tempo` ticks (readme §3.5), per channel:
- **Fetch tick** `tempo − gatetimer`: the next row is read. A new note there (not with command 3) gets gate off and the hard-restart ADSR, unless the new instrument's gate timer has `$40` (legato) or `$80` (no hard restart, gate off only). A key off also takes effect at fetch (measured).
- **Tick 0:** instrument change, note init (first-frame wave, ADSR, table pointers), one-shot commands 5..F. The new note's wave and pulse tables start on the next tick (measured for pulse).
- **Every tick:** a wave-table step. When it set no pitch, realtime command 1..4 or the instrument vibrato runs, except on tick 0. The pulse table is skipped on the fetch tick and on tick 0 of a pattern's last row, where the order list advances (measured: one row before the new pattern).
- **Filter table:** runs every tick before the channels, and the filter registers are written before the channels too. A filter pointer or a B/C/D command takes effect a tick later (measured).

Facts measured with the oracle (2026-10-16):

| Item | Measured behaviour |
|---|---|
| Note table | GoatTracker's own table, the same for PAL and NTSC; A-4 = `1D46` (440.1 Hz on PAL). Read from a packed song with all 96 notes. |
| Order repeat `Dn` | the next pattern plays n + 1 times (`D0` once; the readme says R0 is 16) |
| Wave-table delay `01`-`0F` | waits L ticks, then the row's note on the next tick (a step lasts L + 1 ticks) |
| Wave-table `F0`-`FE` | 5..F act as one-shot commands; 1..4 apply one step on that tick and do not keep running; no other realtime effect on that tick |
| Wave-table note `E0`-`FF` | undocumented; GoatTracker reads past its note table. SIDTracker clamps to B-7 |
| Pulse-table time `00` | 256 ticks (the counter wraps) |
| Tie note `3 00` | the pitch jumps on tick 1 (it is a tone portamento step) |
| Vibrato rhythm | direction changes every speed + 2 pitch steps; the first half cycle (up) is half as long; steps count only on ticks where the pitch moves. Verified for speeds 1..32 |
| Instrument vibrato delay | counts realtime ticks on which the wave table left the pitch alone; it and the vibrato phase restart while commands 1..3 run |
| Packer optimisations | `gt2reloc`'s default optimised players store pulse widths in fewer bits (`0E0 + 20` gives `101`). The oracle packs with `-I0`, the editor-equivalent player |

## Oracle

`tools/oracle/run.sh GT2RELOC song.sng…` takes each song through three steps:
1. Packs it with GoatTracker's `gt2reloc -I0`.
2. Runs the packed PSID on our own 6502 tracer (`tools/oracle/sidtrace.cpp`), which logs the SID registers after every frame.
3. Compares that log with SIDTracker's importer and player, tick for tick (`gtcompare`).

The comparison ignores bits the SID ignores, and the frequency and pulse of voices without a waveform. `gt2reloc` and the packed player are GoatTracker's (GPL). They run as separate programs and only their output is used, never their code.

Result over 60 s (3,000 frames) per example song:

| Song | Frames that differ | Notes |
|---|---|---|
| consultant, dojo, hyperspace, sixpack, tempo2test, wavecmdtest | 0 | |
| 2xtest | 4 | instrument 0 at the start |
| transylvanian | 8 | vibrato after a parameter change |
| cabrinigreen | 23 | vibrato after a parameter change |
| funktest | 46 | per-channel tempo (see below) |
| everlasting | 63 | wave-table note above B-7 (undefined) |
| ghosttrackers | 91 | vibrato phase details |
| sanction, unleash | ~2,600 | per-channel tempo `F83`..`FFF` throughout |

## Unknowns

- **Per-channel tempo (`F83`..`FFF`).** SIDTracker's six tracks share one row clock, so it is applied to all tracks. The import report counts it as approximated.
- **Note-independent speeds** (speed table left byte bit 7). The readme gives only "right side = divisor"; the formula `semitone step >> right` is a guess. No example song uses it.
- **Vibrato phase when 4XY changes its parameter on a held note.** A few frames differ in cabrinigreen, transylvanian and ghosttrackers.
- **The gate timer used before an instrument is set** (instrument 0, 2xtest).
- **Multispeed** (`-S2`..): an editor and packer option, not stored in the song. 2xtest is compared at 1x.
- **`D0` repeat.** The readme says 16 repeats; the packed player plays the pattern once. SIDTracker follows the player.
