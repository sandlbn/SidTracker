# Hardware test plan (M0–M4 acceptance on the device)

Nothing in SIDTracker has run on a Tracker yet. Host tests, the simulator
and the QEMU benchmark pass (`make check`, `make bench-m4`); everything
below needs the real board. Run the sections in order, and stop at the first
failure. Record results (with the date and image name) in this file and the
numbers in `docs/audio.md`.

## 0. Before flashing: the way back

Do this first in every session (APPROACH.md §1, `docs/firmware-map.md`).

1. SD card `/Firmware` holds **only** the stock image
   (`PolyendTracker_1.9.2.ptf` or the one in use) and **one** test image.
   Everything else goes to `/OT_hold`. The rear reset button installs a
   random `.ptf` from `/Firmware`.
2. Copy `build/PolyendTracSIDTRv064.ptf` (24 characters; `make` checks it)
   into `/Firmware`.
3. Know the three ways back: (A) stock Config > Firmware update, (B)
   `/Firmware/ot_restore.txt` naming the stock image (checked before any
   driver starts, renamed `.done` after use), (C) the rear reset button with
   only stock in `/Firmware`.
4. Flash from stock: Config > Firmware update > `PolyendTracSIDTRv064.ptf`.

## 1. M0: platform bring-up

| # | Check | Pass |
|---|---|---|
| 1.1 | Boots to the Pattern screen (title "SIDTRACKER", F1–F8 labels) | |
| 1.2 | USB: device shows as 1209:0001 "SIDTracker"; the CDC serial port prints the boot diagnostics | |
| 1.3 | Every button, all 48 pads and both jog wheel directions act; one detent = one step (raw-detent mode). (Until 0.5 the Master page had an INPUT log for this; removed as bring-up code.) | |
| 1.4 | Power switch: hold 1.5 s powers off; the next boot restores the workspace ("Workspace restored") | |
| 1.5 | Pads play notes on headphones **and** line out (default instrument) | |
| 1.6 | SD: File > SAVE NEW creates `/SIDTracker/Projects/SONG001.sidt`; LOAD reads it back | |
| 1.7 | 10 minutes of playback: "Audio underruns" stays 0 | |
| 1.8 | Underrun counter works: temporarily build with a busy-wait of 600,000 cycles in `Engine::fill`; the count must climb. Then rebuild without it. | |
| 1.9 | Way back: `ot_restore.txt` → stock boots; stock Config > Firmware update → SIDTracker boots again; Config > F7 FIRMWARE, F8 INSTALL twice → stock boots | |

## 2. M1: one SID voice

| # | Check | Pass |
|---|---|---|
| 2.1 | Instrument 02 with AD 00, SR F0, first wave 21 (saw); A-4 on track 1; tuner at the line out reads 440 Hz ±1 cent (Fs = 44,117.6 Hz is compensated through the SID clock step) | |
| 2.2 | ADSR audible: attack F (8 s) rises slowly, release F fades slowly, sustain 0 makes a pluck | |
| 2.3 | Waveforms: first wave 11 / 21 / 41 / 81 give triangle / saw / pulse / noise | |
| 2.4 | Benchmark: SID1 voices section shows cycles; total under 70 % | |

## 3. M2: one complete SID

| # | Check | Pass |
|---|---|---|
| 3.1 | Three notes on tracks 1–3 play together | |
| 3.2 | Hard sync: wave table `23 00` on track 1 with track 3 playing lower: sync sweep sound | |
| 3.3 | Ring modulation: first wave `15` on track 1, track 3 running: bell tones | |
| 3.4 | Filter: instrument filter table `90 F1`, `00 20`, `40 02`, `FF 03`; cutoff sweep audible on 6581 and 8580 (Settings), clearly different | |
| 3.5 | Pulse width sweep (default instrument 01) audible | |
| 3.6 | 30 minutes of the worst-case pattern (every row a note on tracks 1–3, filter sweep) without underruns; peak under 70 %; record the section cycles in `docs/audio.md` | |

## 4. M3: dual SID

| # | Check | Pass |
|---|---|---|
| 4.1 | Six tracks playing at once; SID balance 100 puts SID1 left, SID2 right | |
| 4.2 | SID1 = 6581, SID2 = 8580 (Settings): the same filtered pattern sounds different per side | |
| 4.3 | Benchmark F1 STRESS (all six voices, both filters, resonance F): 30 minutes, no underruns, peak recorded | |
| 4.4 | Switching a model while playing does not click badly or crash | |

## 5. M4: tracker (release 0.1 candidate)

| # | Check | Pass |
|---|---|---|
| 5.1 | Write a complete song on the device (patterns, instruments with tables, order list with transpose and repeat) | |
| 5.2 | Save, power off, power on, load: identical | |
| 5.3 | Pattern editing while playing does not glitch audio | |
| 5.4 | Pattern screen frame rate stays smooth (telemetry over USB serial) and the FT81x display list does not overflow (Pattern screen is the heaviest: ~18 rows x 6 tracks) | |

## 6. M8 / M9: extended engine, .sid export

| # | Check | Pass |
|---|---|---|
| 6.1 | Drum column, kit WAVs from `/SIDTracker/Kit`, EXTENDED screen pages; the benchmark page (Shift+Master) shows PCM and effects rows | |
| 6.2 | All effects on: CPU on the benchmark page (Shift+Master; record avg / peak, effects row), no underruns over 10 minutes | |
| 6.3 | File > F6 twice: `/SIDTracker/SID/NAME.sid` written; it plays in a SID player (Phosphor) like on the Tracker (DUALDEMO: two SIDs) | |
| 6.4 | Power: hold the power button 1.5 s -> off. Unplug and replug USB: stays dark. Press the button: boots. `ot_restore.txt` still works while "off" (it runs before the gate) | |
| 6.5 | Instrument > F7 SOUNDS: load a few presets from each category, play them on the pads; write an arpeggio (minor, up-down, 2 ticks) and play it | |
| 6.6 | Config > THEME: each theme on every screen; the theme and volume survive a power cycle | |
| 6.7 | Shift+Power: "Saved SHOTnnnn.bmp"; the BMP on the card shows the screen the right way up; Power held alone still switches off | |
| 6.8 | Master: mixer rows act (volume, balance, drums, effects, mixes); meters move with SID1 / SID2 / drums / output, OUT turns red when clipping; CPU line matches the benchmark page | |
| 6.9 | Instrument > F2 DESIGN: hold a pad, turn CUTOFF / RESONANCE / PULSE WIDTH / SWEEP: the note replays with each change; switch away and back: the values are read back | |

## Results

| Date | Image | Sections passed | Notes |
|---|---|---|---|
| 2026-10-16 | PolyendTrackerSTv010.ptf | first run: boots, SD load / save, playback of imported GoatTracker songs (owner: "all is working") | itemised checks and benchmark numbers (1.3, 1.7, 2.4, 3.6, 4.3) not yet recorded |
| 2026-10-16 | PolyendTrackerSTv040.ptf | M8 features work (owner: "all seems to be working") | CPU: song 37 % avg / 69 % peak; stress 39 %; all effects on ≈ 68 % (docs/audio.md). Master page section table still said "not built" for PCM / effects |
| 2026-10-16 | (host export, release 0.6.1) | the five example `.sid` files (DUALDEMO, NIGHTDRIVE, PCMGROOVE, FILTERS, CHIPMARCH) play in Phosphor / libsidplayfp (owner: "all sid are working"), one and two SIDs, VBI and CIA tempo | 6.3 on the device (F6 export on the Tracker) still open |
| 2026-10-16 | PolyendTrackerSTv062.ptf | flashed by `ot_restore.txt` (renamed to `.done`); the unit wrote `/SIDTracker/settings.txt` (volume 55, theme 2 = GREEN SCREEN), so themes and saved settings work on the device | the jog push could not confirm Config > FIRMWARE UPDATE: F7 / F8 in 0.6.3 |
