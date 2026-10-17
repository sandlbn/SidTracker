# GoatTracker to SIDTracker mapping

How GoatTracker songs and instruments map onto SIDTracker's model (PLAN.md
§8, §16), as implemented in `src/compatibility/goat/` and tested in
`tests/goat_test.cpp` and `tests/app_test.cpp`. File formats and measured
playback: `docs/goattracker-format.md`.

## What maps 1:1

| GoatTracker | SIDTracker |
|---|---|
| Instruments 1-63: AD, SR, wave/pulse/filter pointers, vibrato param and delay, gate timer (`$80` / `$40` flags), first-frame wave, name (16 chars) | `Instrument` with the same fields and meanings |
| Wave, pulse, filter, speed tables (255 rows) | the same four tables |
| Pattern rows: note, instrument, command `0`-`F`, data byte | `Cell` with the same values; `---` key off is shown as `===` |
| Commands 1-F, wave-table commands `F0`-`FE` | the same commands with GoatTracker's timing (`docs/commands.md`) |
| Hard restart ADSR (editor option `-A`, default `0F00`) | song setting, default `0F00` |
| PAL / 50 Hz frames, tempo 6 at start | 125 BPM (50 Hz ticks), speed 6 |

Instruments and tables need no conversion, so instrument-level
compatibility (PLAN.md level 1) is exact.

## Patterns and order lists (levels 2 and 3)

GoatTracker has three single-channel order lists, each with its own patterns
(up to 128 rows), transposes and repeats. SIDTracker has one order list of
six-track patterns. Import lays the three channels side by side on one row
clock:
- A cut is made wherever any channel starts a pattern, and at the loop point. Identical cuts share a SIDTracker pattern, and consecutive identical entries become repeats.
- Channel transposes are applied to the notes.
- Each pattern records which tracks start a GoatTracker pattern there (`Pattern::starts`). The player uses it for GoatTracker's pulse skip at order advances, and the exporter uses it to rebuild each channel's own patterns.
- **Loop.** The intro is the latest channel intro and the loop is the least common multiple of the channel loops, so playback repeats exactly as in GoatTracker. Channels whose loops differ in length drift apart in GoatTracker. When the exact loop does not fit (more than 128 patterns or 255 order entries), one pass of the longest loop is imported and the channels re-align at the loop. The report says so.
- GoatTracker channels 1-3 become tracks 1-3 (SID1); tracks 4-6 stay empty. Subtune 1 is imported, and the report mentions the others.

Export reverses this:
- Each track becomes a GoatTracker channel. A channel continues its GoatTracker pattern across SIDTracker patterns that do not mark it as starting, so imported songs get their channel patterns back.
- Transposes and repeats become order-list commands.
- The song's speed becomes `Fxx` on the first row.

Verified:
- **Round trip:** all 14 example songs play the same register stream after import → export → import.
- **Native songs:** a song made in SIDTracker plays frame for frame the same in GoatTracker's player as in SIDTracker (`tools/oracle/sidt2sng` + `run.sh`).

## Compatibility report (PLAN.md §8, §16)

Import of GoatTracker's example `unleash.sng`:

```
97% commands mapped directly (898 of 923)
25 commands approximated
! F83-FFF per-channel tempo applied to all tracks (25)
83 patterns, 92 order entries, 2264 rows, loop at row 2248
```

| Approximated / unsupported | Why |
|---|---|
| `F83`-`FFF` per-channel tempo | one row clock for all tracks |
| `DXY` with X ≠ 0 (timing mark) | no musical effect; ignored |
| Note-independent speeds | formula not documented (docs/goattracker-format.md) |
| Transposed notes outside C-0..B-7 | clamped |
| Channel loops of different length | re-aligned at the loop |

Export check (shown before writing, File screen F2):

| Warning | Cause |
|---|---|
| SID2 tracks 4-6 are not exported | GoatTracker has three channels |
| BPM other than 125 / 150 | GoatTracker ticks once per frame; 150 = NTSC timing (`-N`) |
| Hard restart ADSR other than `0F00` | an editor option in GoatTracker, not saved in the song |
| Notes above G#7 | GoatTracker patterns end at G#7 |
| Transposes outside −15..+14 | GoatTracker's range |
| Imported channel timing | pulse skipping may differ slightly |

## Not covered yet

- GoatTracker Stereo (six channels, two SIDs): the format is not in the 2.77 documentation.
- Subtunes other than the first: import only.
- Older formats (GTS2-4, GTI2-4): refused with a message.
