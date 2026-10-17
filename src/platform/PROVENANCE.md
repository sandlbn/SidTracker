# Vendored platform code: provenance

Copied from OpenTracker, our clean-room firmware for the same hardware
(`~/Projects/ReversedPolytracker-1.9x`, branch `opentracker`, commit
**`0849cb0`** = `c69d753` + `3b60137` open Roboto fonts + MIT licence).
OpenTracker's own code is MIT (`LICENSE.OpenTracker` here); third-party
code keeps its licence (`third_party/teensy3/LICENSE.txt`,
`third_party/fatfs/LICENSE.txt`, `third_party/fonts/*`).

`tools/sync_hal.sh [OPENTRACKER_REIMPL_DIR]` diffs every file below against
the recorded commit. Fixes flow both ways by hand (APPROACH.md §1).

## File map (SIDTracker path <- OpenTracker `reimpl/` path)

| SIDTracker | OpenTracker |
|---|---|
| `src/platform/hal_api.h` | `src/core/hal_api.h` |
| `src/platform/{board,i2c_bus,diag,diag_log,fw_update,midi_ports,selftest,radio_si4703}.{h,cpp}`, `hal.h`, `k66.h`, `hw_math.h` | `src/hal/` same names |
| `src/platform/timer/telemetry.{h,cpp}` | `src/hal/telemetry.*` |
| `src/platform/display/ft81x.{h,cpp}` | `src/hal/ft81x.*` |
| `src/platform/input/{input,pad_leds}.{h,cpp}` | `src/hal/input.*`, `src/hal/pad_leds.*` |
| `src/platform/audio/audio_sgtl5000.{h,cpp}`, `audio_sgtl5000_dap.h` | `src/hal/audio_sgtl5000*` |
| `src/platform/storage/{sdhc,sd_storage,nvstore_eeprom}.{h,cpp}` | `src/hal/` same names |
| `src/platform/core/formats/crc32.*` | `src/core/formats/crc32.*` (used by `ft81x.cpp`) |
| `src/platform/core/ui/{gfx,theme}.*`, `buttons.h`, `fonts/*` | `src/core/ui/` same names |
| `third_party/teensy3/`, `third_party/fatfs/` | `third_party/` same names |
| `third_party/fonts/` (licence files only) | `third_party/fonts/` |
| `tools/check_image.py` | `tools/check_image.py` |
| `sim/src/*` except `main.cpp`, `sim/third_party/*` | `sim/src/*`, `sim/third_party/*` |
| `tests/test.h`, `tests/mem_storage.h`, `tests/bench_m4/*` | `tests/` same names |

Not vendored: OpenTracker's `src/main.cpp`, `src/app_main.cpp`, everything
else under `src/core` (its sample engine and UI), `sim/src/main.cpp`
(SIDTracker has its own entry points: `src/app/device_main.cpp`,
`sim/src/main.cpp`).

## Local changes

Each is marked `SIDTracker local change (PROVENANCE.md)` in the source.

1. `audio/audio_sgtl5000.{h,cpp}`: underrun counter (plan §6). After the
   fill callback returns, the TX ISR re-reads the DMA source address; if the
   DMA is already reading the half that was just written, the block was late
   and `underruns()` counts it. UNVERIFIED on hardware: the first M0 session
   must provoke a late block (busy loop in the callback) and see the count.
2. `input/input.{h,cpp}`: `setRawDetents(bool)`: one event delta per
   detent without the spec 09 §4 acceleration, for the jog wheel as value
   editor. The ≥100 ms poll and the 4-counts-per-detent decode are unchanged.

3. `third_party/teensy3/usb_desc.h`: with `-DSIDT_USB_STRINGS` (the
   Makefile passes it) the USB manufacturer and product strings read
   "SIDTracker" (DECISIONS.md 4). VID:PID stay 1209:0001, the pid.codes test
   PID, until a dedicated PID is registered (release blocker).

4. `board.cpp`, `hal.h`: the stock power flag, ported by hand from
   OpenTracker commits `874ab3e` + `141ee45` (tested there on hardware):
   switching off writes EEPROM byte 0 = 0; `bootPowerGate()` (called from
   `device_main.cpp` after the `ot_restore.txt` check) keeps the unit off
   until the power button is pressed, then writes 1 and resets. Drop this
   entry when the recorded commit moves past `141ee45`.

## Kept although unused

- `radio_si4703.*`: `board.cpp` parks the tuner in reset and calls
  `radio()` from `service()`, `softOff()` and `selectInput()`. SIDTracker
  never hands an `IRadio` to the app. Dropping it means editing
  `board.cpp`; deferred until there is a reason (PLAN.md §3 "do not rewrite
  working hardware support without a reason").
- `audio_sgtl5000_dap.h`: the codec DAP (Master EQ) stays wired as in
  OpenTracker; SIDTracker never calls `applyMasterDsp`.
- `nvstore_eeprom.*`: linked because `board.cpp` / `fw_update.cpp` use the
  EEPROM byte 0 update flag. SIDTracker keeps its settings on the SD card,
  because an EEPROM write leaves HSRUN and halves the core clock with audio
  running (docs/audio.md, unknowns).
