# Current state: repository audit

Plan §21 Task 1. SIDTracker starts from OpenTracker, the clean-room firmware
for the original Polyend Tracker in `ReversedPolytracker-1.9x/reimpl`
(branch `opentracker`). The vendored copy is taken from **HEAD `0849cb0`**:
this is `c69d753` (cited in APPROACH.md) plus `3b60137` (open Roboto /
Roboto Mono fonts uploaded to FT81x RAM_G, a new `ft81x.*` font path,
`hal_api.h` font-handle constants) and `0849cb0` (MIT `LICENSE`, README note).
Only `reimpl/`, `docs/specs/` and `docs/*.md` of that repo were read
(clean-room rule, APPROACH §1). Paths below are relative to that repo unless
they start with `src/platform/` (SIDTracker).

## Build system

| Item | Value | Source |
|---|---|---|
| Toolchain | `arm-none-eabi-gcc` + newlib nano (tested 16.2), GNU make, Python 3 | `reimpl/README.md` "What to install" |
| CPU flags | `-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`, `-O2 -g`, gc-sections | `reimpl/Makefile` |
| Defines | `__MK66FX1M0__ F_CPU=180000000 F_BUS=90000000 USB_MIDI_SERIAL`, Serial5 buffers 512, `OT_USB_VID/PID=0x1209/0x0001` | `reimpl/Makefile` |
| Languages | C++17 (`-fno-exceptions -fno-rtti`), C11 | `reimpl/Makefile`, `reimpl/ARCHITECTURE.md` |
| Linker script | PJRC `mk66fx1m0.ld` (flash 1 MB at 0, RAM 256 KB at 0x1FFF0000) | `reimpl/third_party/teensy3/mk66fx1m0.ld` |
| Outputs | `build/opentracker.elf/.hex/.map`; `.hex` copied to a 24-character `PolyendTrac*.ptf` after `tools/check_image.py` passes | `reimpl/Makefile` |
| Variants | `make SELFTEST=1` (always runs the hardware selftest), `make -C tests` (host, ASan/UBSan), `make -C tests bench` / `bench-m4` (QEMU `mps2-an386 -icount`), `make sim` (SDL2) | `reimpl/Makefile`, `reimpl/tests/Makefile`, `reimpl/sim/README.md` |

The image-name length check is a hard `$(error)` in the Makefile; the image
check (flash config, extent, record types) runs on every `.ptf` build. See
[firmware-map.md](firmware-map.md).

## Firmware layout

`src/core/**` is platform independent and never touches registers; it talks
to hardware only through `src/core/hal_api.h` (`IDisplay`, `IAudioOut`,
`IStorage`, `IMidiPort`, `IInput`, `ILeds`, `IClock`, `IRadio`, `INvStore`).
`src/hal/**` implements them for the MK66 board (`reimpl/ARCHITECTURE.md`
"Rules"). Device glue: `src/main.cpp` (boot), `src/app_main.cpp` (SDRAM
layout, `HalRefs` wiring, telemetry). Third party: PJRC Teensy 3 core
(`third_party/teensy3`, upstream `7f107ee`, one patch for the USB IDs,
`PATCHES.txt`), FatFs R0.15a (`third_party/fatfs`), Roboto fonts
(`third_party/fonts`).

## Reverse-engineered subsystems (specs) and their implementation

| Subsystem | Spec | Implementation | Field status |
|---|---|---|---|
| Clocks, SDRAM, pins, power | 01 §0–§3, §10 | `hal/board.*`, `k66.h`, `hw_math.h` | runs on hardware (APPROACH §1) |
| Display FT81x | 01 §4 | `hal/ft81x.*` | runs |
| Audio SGTL5000 / I2S0 / DMA | 01 §5, 08, 04 §1.1 | `hal/audio_sgtl5000.*` | runs; DUF, TX-first and FCONT fixes came from field failures (code comments) |
| SD card / FatFs | 01 §6 | `hal/sdhc.*`, `hal/sd_storage.*` | runs; ADMA2 and 45 MHz self-checked with fallbacks |
| Buttons, pads, pad LEDs, encoder | 01 §7, 09 | `hal/input.*`, `hal/pad_leds.*` | runs; pad map checked on the device 2026-10-15 (`input.cpp` comment) |
| MIDI DIN + USB | 01 §8, §9 | `hal/midi_ports.*` | implemented |
| FM radio Si4703 | 01 §5.6 | `hal/radio_si4703.*` | implemented; dropped by SIDTracker |
| Update hand-off, EEPROM | 01 §10, 02 §9 | `hal/fw_update.*`, `hal/nvstore_eeprom.*` | ot_restore path used in the field (APPROACH §1) |

Note: the header of `docs/HARDWARE_TESTING.md` still says "OpenTracker has not
run on real hardware yet"; the code comments and APPROACH.md record field
results since then. Treat that line as stale.

## Known hardware

MK66FX1M0 at 180 MHz HSRUN, 90 MHz bus and FlexBus, 16 MB SDRAM, FT81x 800x480
on SPI2, SGTL5000 on I2C0 + I2S0, two TCA8418 keypad scanners, IS31FL3731 pad
LEDs, FTM2 quadrature encoder, SDHC 4-bit, UART4 MIDI, USB device. Details and
pins: [hardware.md](hardware.md).

## Unknown hardware

From spec 01 §12 and the `UNVERIFIED` markers in `reimpl/src/hal/board.h`:

1. USB Storage Mode: no MSC class, stock hook is an empty stub (spec 01 §6.1, §12.1).
2. SDRAM part and the meaning of the mode-register address 0x70001800 (§12.2; `board.cpp` "UNVERIFIED").
3. Exact FT81x part (REG_ID 0x7C only) (§12.3).
4. Physical role of PTA24 (rail enable), PTC19 (status LED), PTE26 (radio switch), PTA1 (update line) (§12.6; `board.h` UNVERIFIED on PTA24, PTC19, mute PTD6, PTE26).
5. DMA channel numbers derived from allocation order, not observed (§12.7).
6. Fuel gauge identity and presence (§7.6, §12.5; `kAddrFuelGauge` UNVERIFIED). OpenTracker does not use it (README "Not included").
7. Codec I2S_CTRL 32·Fs setting on a 64·Fs bus works but is unexplained (`audio_sgtl5000.cpp`, spec 01 §5.2 [I]).
8. Normal-operation codec input levels (§12.9; `kLineInLevel`, `kMicHighGainDb` UNVERIFIED).
9. Whether SDHC DMA reaches SDRAM (`kSdDmaToSdram = false`, `board.h`).
10. Encoder CW direction and detent = 4 counts (spec 09 "Open unknowns").
11. Pad brightness Low/Medium/High mapping (§12.4).
12. MIDI DIN TX inversion reason (`midi_ports.cpp` UNVERIFIED [O/I]).
13. Low-power off (VLPS at 16 MHz) untested, current not measured (`docs/HARDWARE_TESTING.md` §0).

## Reusable code (vendored into SIDTracker)

| OpenTracker | SIDTracker | Use |
|---|---|---|
| `src/core/hal_api.h` | `src/platform/hal_api.h` | interface boundary, unchanged except `IRadio` unused |
| `hal/board.*`, `hal.h`, `k66.h`, `hw_math.h`, `i2c_bus.*`, `diag*`, `fw_update.*`, `midi_ports.*`, `selftest.*`, `telemetry.*` | `src/platform/` (the working tree currently has `telemetry.*` under `src/platform/timer/`, APPROACH §6) | bring-up, `HalSet`, power, update, diagnostics |
| `hal/ft81x.*` | `src/platform/display/` | `IDisplay` |
| `hal/input.*`, `hal/pad_leds.*` | `src/platform/input/` | `IInput`, `ILeds` |
| `hal/audio_sgtl5000.*` | `src/platform/audio/` | `IAudioOut` |
| `hal/sdhc.*`, `sd_storage.*`, `nvstore_eeprom.*` | `src/platform/storage/` | `IStorage`, `INvStore` |
| `third_party/teensy3`, `third_party/fatfs` | `third_party/` | runtime, FatFs |
| `tools/check_image.py` | `tools/` | image gate |

Coupling to resolve when vendoring: `board.cpp` calls `radio()` (in
`service()`, `softOff()`, `selectInput()`), so `radio_si4703.*` is needed or
must be stubbed; `ft81x.h` includes `ui/fonts/fonts.h` (129,248-byte font blob
uploaded at init, `font_data.h`); the selftest is linked as the fallback when
no `otAppMain` is present (`main.cpp`).

## Current boot process

From `reimpl/src/main.cpp` and `reimpl/src/hal/board.cpp`:

1. Teensy `ResetHandler`: watchdog off, clocks to 180/90 MHz HSRUN, SysTick 1 ms, static constructors (spec 01 §1, spec 08 §1).
2. `diagInit()`: fault vectors, SysTick hook, USB CDC log drain (`hal/diag.h`).
3. `boardEarlyInit()`: status LED on, `SIM_CLKDIV1` check, power-switch pin, **PTA24 rail HIGH**, card detect, SDHC init + FatFs mount. Deliberately before everything else so the SD-only escape works.
4. If mounted: `checkRestoreTrigger()` (`/Firmware/ot_restore.txt`, see [firmware-map.md](firmware-map.md)).
5. `boardInit()`: I2C0/I2C2 recovery, SDRAM init + 1 MB test, DWT on, FT81x init + font upload, codec + I2S/DMA (muted), Si4703 parked in reset, keypads + encoder, pad LEDs, MIDI, status LED off.
6. `otAppMain(halSet())` if linked and SDRAM passed; otherwise (or on return) `runSelftest()`.

Unlike stock, boot never gates on the EEPROM power flag (`board.cpp` comment
on §0 steps 1–2).
