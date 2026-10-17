# Firmware map: image layout, boot, update and recovery

Plan §3: image layout and limits, boot sequence, reusable entry points,
whether the stock bootloader/update path can load SIDTracker safely, and the
way back to stock. Sources: OpenTracker (`ReversedPolytracker-1.9x`, branch
`opentracker`, HEAD `0849cb0`).

## Image format and limits

| Rule | Value | Source |
|---|---|---|
| Container | Intel HEX, file extension `.ptf`, in `/Firmware` on the SD card | `reimpl/Makefile` (`.hex` copied to `.ptf`); spec 02 §9 |
| Name prefix | `PolyendTrac`, else the stock updater does not list it | `Makefile` comment; `fw_update.h isUpdateImageName` |
| Name length | **exactly 24 characters** (stock `PolyendTracker_1.9.2.ptf`); the bootloader rejected a 45-character name and then installs a random file | `Makefile` `$(error)`; `docs/HARDWARE_TESTING.md` §1 |
| SIDTracker name | `PolyendTracSIDTRv064.ptf` for 0.6.4 (until 0.6.3: `PolyendTrackerSTv063.ptf`). Rules: exactly 24 characters (bootloader), `PolyendTrac` prefix and `.ptf` (stock Firmware update list, and our Config filter); the 9 characters between are free | APPROACH §1 |
| Flash config 0x400–0x40F | `FF×12, DE, F9, FF, FF` (FSEC 0xDE unsecured, mass erase on; FOPT 0xF9) | `tools/check_image.py` |
| Vector table | SP in (0x1FFF0000, 0x20030000]; reset vector Thumb, inside the image, not in 0x400–0x40F | `check_image.py` |
| Extent | every byte below **0xD6460** (stock 1.9.2 end, 877,664 B); the rest of flash and FlexNVM may hold bootloader / identity data | `check_image.py` |
| HEX records | only 00, 01, 02 | `check_image.py` |
| `.eeprom` section | stripped (`objcopy -R .eeprom`) | `Makefile` |

`isUpdateImageName()` (used by the Settings picker) accepts 15–96 characters,
so it does **not** enforce the 24-character rule; only the Makefile does. Do
not put long-named images in `/Firmware`.

## Flash layout (linker)

`.vectors` and `.startup` at 0, `.flashconfig` forced to 0x400, then
`.text`, `.rodata`, init arrays, `.ARM.exidx`, and the `.data` load image at
`_etext` (`third_party/teensy3/mk66fx1m0.ld`). The vector table is copied to
`_VectorsRam` in SRAM by the Teensy reset code; `diag.cpp` and `DMAChannel`
patch vectors there.

## Boot sequence and entry points

| Step | Function | Notes | Source |
|---|---|---|---|
| 1 | `ResetHandler` (Teensy) | clocks, watchdog off, SysTick, constructors | spec 01 §1; spec 08 §1 |
| 2 | `main()` → `diagInit()` | fault/SysTick/drain vectors, USB CDC log | `src/main.cpp`; `hal/diag.h` |
| 3 | `boardEarlyInit()` | rail PTA24 HIGH, SD detect + mount only | `hal/board.cpp` |
| 4 | `checkRestoreTrigger(storage())` | before any other driver | `hal/fw_update.cpp` |
| 5 | `boardInit()` | I2C, SDRAM + 1 MB test, DWT, FT81x, codec/I2S/DMA, keypads, LEDs, encoder, MIDI | `hal/board.cpp` |
| 6 | `otAppMain(HalSet&)` (weak) | only if `boot.sdramOk`; diag watchdog off | `src/main.cpp` |
| 7 | `runSelftest()` | fallback if no app, SDRAM failed, or the app returns | `hal/selftest.*` |

Reusable as is: `boardEarlyInit`, `boardInit`, `bootReport`, `halSet`,
`service` (main-loop HAL service: MIDI, input, LEDs, card detect, power
switch), `powerOff`/`setPowerOffHook`, `softOff`, `requestFirmwareUpdate`,
`checkRestoreTrigger`, `isUpdateImageName`, `eepromPartitionUsable`,
`nvStore()`, `diagLog*`, `diagStage`, `runSelftest` (`hal/hal.h`,
`fw_update.h`, `diag.h`, `nvstore_eeprom.h`). In SIDTracker they live under
`src/platform/` with the same names. SIDTracker's `src/app/main.cpp` is
planned to provide `otAppMain` (APPROACH §6). `selectInput`/`lineIn` and `radio()` are
OpenTracker recorder hooks; SIDTracker does not call them.

## Update hand-off (how an image gets flashed)

The MCU application never programs its own flash. `requestFirmwareUpdate(fs,
name)` (`hal/fw_update.cpp`, spec 02 §9, spec 01 §10):

1. refuses unless the name passes `isUpdateImageName`, `/Firmware/<name>` exists and `eepromPartitionUsable()` (FlexRAM already partitioned as EEPROM, EEERDY set, RAMRDY clear), so the Teensy core can never issue a repartition (PGMPART) on the unit;
2. deletes and rewrites `/Firmware/_fwinfo` with the raw name (no terminator, no header);
3. writes EEPROM byte 0 = 1 with IRQs off, then restores `SIM_CLKDIV1` OUTDIV3 = 1;
4. waits 10 ms, drives **PTA1 LOW** (the only code allowed to touch PTA1, `board.h`) and spins.

An external bootloader / supervisor is then expected to read `_fwinfo` and
flash the file (spec 02 §9 [I]; spec 01 §10 "presumably").

`checkRestoreTrigger()`: if `/Firmware/ot_restore.txt` exists, its first line
(trimmed) names an image in `/Firmware`; the file is renamed to
`ot_restore.done` **before** the hand-off so a rejected image cannot cause a
reset loop; then `requestFirmwareUpdate`. It needs only the rail and the SD
card (`main.cpp` comment, `docs/HARDWARE_TESTING.md` §3 B).

## Can the stock update path load SIDTracker safely?

Yes, under the same rules OpenTracker uses in the field (APPROACH §1;
`docs/HARDWARE_TESTING.md`):

- install from stock **Config > Firmware > Firmware update**, choosing the 24-character `PolyendTrac*.ptf`;
- every image passes `check_image.py` (cannot lock the MCU; stays inside the stock extent);
- the update flag, `_fwinfo` format and PTA1 sequence are identical to stock, so SIDTracker can hand back to any stock image;
- residual risk: the bootloader itself is not specified (only its observed behaviour), and "flashing is never risk-free" (`HARDWARE_TESTING.md` §0).

## Way back to stock

In order of preference (`docs/HARDWARE_TESTING.md` §3):

| Path | Needs | How |
|---|---|---|
| A. Settings picker | display, buttons, SD | SIDTracker Settings > Firmware update → `PolyendTracker_1.9.2.ptf` (same `requestFirmwareUpdate`) |
| B. Restore file | rail + SD only | `/Firmware/ot_restore.txt` with first line `PolyendTracker_1.9.2.ptf`, power on |
| C. Rear reset button | nothing of ours | pin into the recessed button next to USB: the bootloader installs a **random** `.ptf` from `/Firmware` |
| D. Polyend Tool | computer | vendor emergency flash, if the SD route fails |

SD rule (APPROACH §1): `/Firmware` holds only stock 1.9.2 plus at most one
test image; everything else goes to `/OT_hold`, because of path C. Every
milestone's first hardware session proves path B before testing anything else.

## EEPROM byte 0 and the power flag

Stock uses EEPROM byte 0 as both power flag and update flag and gates boot on
it (spec 01 §10). OpenTracker never gates boot on it (`board.cpp`
`boardEarlyInit` comment) and writes it only in `requestFirmwareUpdate`.
`INvStore` offsets start at physical byte 10, so bytes 0–9 are unreachable
through it (`hal_api.h`, `nvstore_eeprom.cpp`). If byte 10.. does not hold
the `EEPROM` magic, stock reads the update flag as 1 (spec 02 §9): SIDTracker
must not overwrite the stock config image at byte 10 with its own data
without considering this. See [storage.md](storage.md).

## Unknowns

- The bootloader: where it lives, how it validates images, and why it rejects names ≠ 24 characters (only observed, `HARDWARE_TESTING.md` §1).
- What PTA1 drives physically (spec 01 §12.6).
- What stock does when the `EEPROM` magic at byte 10 is missing (spec 02 §9 "not traced further").
- Flash beyond 0xD6460 and FlexNVM contents: unknown, hence forbidden (`check_image.py`).
