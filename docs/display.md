# Display

Plan §3: LCD controller, framebuffer / display-list model, update mechanism,
RAM_G map, fonts, cost per frame. Sources: OpenTracker
(`ReversedPolytracker-1.9x`, branch `opentracker`, HEAD `0849cb0`). Driver
`reimpl/src/hal/ft81x.*`, reused as `src/platform/display/ft81x.*`.

## Controller and panel

| Item | Value | Source |
|---|---|---|
| Controller | Bridgetek FT81x (EVE2), REG_ID 0x7C; exact part unknown (OUTBITS 0x1B6 suggests FT810/811) | spec 01 §4, §12.3 |
| Interface | SPI2 mode 0, single-wire, MSB first; CS = GPIO PTD15, PD_N = PTD11; **no INT_N line** | spec 01 §4.1; `board.h` |
| SPI clock | 4.5 MHz during power-up (`CTAR0 0x38021001`), 30 MHz after (`0xB8010000`) | spec 01 §4.1; `board.h kSpi2CtarSlow/Fast` |
| Panel | 800 × 480, rotated 180° (`REG_ROTATE` 1), 18-bit RGB with dithering, PCLK 30 MHz (60 MHz / 2) | spec 01 §4.2 |
| Refresh | 1056 × 525 clocks per frame → ≈ 54.1 Hz | spec 01 §4.2 |
| Backlight | FT81x PWM at 9 kHz, duty 0 at init, 127/128 at the first `swap()`; `setBacklight(0..128)`; config Low/Med/High = 20/50/128 | spec 01 §4.2; `hal_api.h brightnessDuty` |
| Clock | internal oscillator (stock); driver retries once with `CLKEXT` and reports `bootAttempt` 1 | `ft81x.cpp init()`; `HARDWARE_TESTING.md` |

Power-up order (spec 01 §4.2, implemented verbatim in `Ft81xDisplay::init`):
PD pulse, `ACTIVE`, poll REG_ID and REG_CPURESET, timing registers, a black
display list + `DLSWAP`, `REG_PCLK = 2` last (starts scan-out), SPI to 30 MHz,
then the font upload.

## Model: display list, no MCU framebuffer

There is no framebuffer in MCU memory. The FT81x renders every scanline from
a **display list** (RAM_DL, 2048 words = 8 KB, `sim/README.md`
"Limitations"). The MCU writes coprocessor commands into the 4 KB ring
RAM_CMD (0x308000); the coprocessor turns them into RAM_DL and `CMD_SWAP`
swaps the list at the next frame (spec 01 §4.3; `ft81x.cpp`).

`IDisplay` (`src/core/hal_api.h`, reused as `src/platform/hal_api.h`) is a
thin builder for that list:

- frame: `dlStart()` … `swap()`;
- state: `clearColorRGB`, `clear`, `colorRGB`, `colorA`, `lineWidth`, `pointSize`, `scissorXY/Size`, `saveContext/restoreContext`, `vertexTranslateX`, `bitmapTransform`, `bitmapSize`;
- primitives: `begin(kPoints|kLines|kLineStrip|kEdgeStrip*|kRects|kBitmaps)`, `vertex2f` (1/16 px), `vertex2ii`, `end`; helpers `rect`, `line`;
- coprocessor widgets: `text`, `number`, `gradient`;
- RAM_G: `writeRamG`, `drawRgb332`, `loadJpeg`/`drawImage`, `readPixels` (screenshot via `CMD_SNAPSHOT2`), `bindFontSet`.

`src/core/ui/gfx.*` (`Gfx`) adds fills, frames, hairlines, round rects,
bars, clipped text, `glyphRun` and icons on top of `IDisplay`, and counts
display-list words (`dlWords()`). APPROACH §1: SIDTracker reuses `gfx` and the
generated fonts, nothing else of OpenTracker's UI.

## Update mechanism

From `ft81x.cpp`:

1. `dlStart()`: flush staged bytes, `waitIdle()` until `REG_CMD_READ == wp` and `REG_DLSWAP == 0` (previous swap done), queue `CMD_DLSTART`, re-bind the custom font handles.
2. Commands are staged in a 1 KB MCU buffer (`stage_`); when full, `flush()` waits for free FIFO space (`4092 − ((wp − rp) & 0xFFF)`), writes the bytes at `RAM_CMD + wp` (wrapping) and updates `REG_CMD_WRITE`.
3. `swap()`: `DISPLAY`, `CMD_SWAP`, flush. Backlight comes on at the first swap.
4. Fault recovery: `REG_CMD_READ == 0xFFF` or > 100,000 busy polls → `REG_CPURESET` 1, read/write pointers 0, reset 0 (spec 01 §4.3). Fonts survive in RAM_G; handles are re-bound at the next `dlStart`.

All SPI traffic is polled byte by byte (the K66 SPI2 FIFO is one entry;
`xfer()`); there is no SPI DMA in OpenTracker. RAM_G bulk writes use a
pipelined burst (about 0.27 µs per byte at 30 MHz, `memWriteBurst`). Display
work therefore runs in the main loop and is preempted by the audio ISR, never
the reverse.

OpenTracker's UI is change-driven: `Ui::dirty()` marks a change and the main
loop renders a full list at most every 16 ms (`ui.cpp kMinFrameMs`,
`renderIfDirty`). `app_main.cpp` logs UI frames, fps, render time (avg/max)
and DL words every 5 s in the telemetry line.

## RAM_G map (1 MiB)

| Range | Use | Source |
|---|---|---|
| 0x000000–0x05DBFF | screenshot band, 800 × 240 RGB565 (`readPixels`) | `ft81x.h` |
| 0x05DC00–0x06CBFF | Games frame 256 × 240 RGB332 (`kRamGGameFrame`) | `ft81x.h`; `hal_api.h` |
| 0x06CC00–0x08C4DF | custom UI fonts (2 sets × 5 roles) + icon sheet, 129,248 B, CRC-checked after upload | `ft81x.h`; `ui/fonts/font_data.h` |
| … –0x0A392F | free | `ft81x.h` |
| 0x0A3930–0x0F1A2F | JPEG cover image, ≤ 400 × 400 RGB565 | `ft81x.h`; `hal_api.h kRamGCover` |
| 0x0F1A30–0x0FFFFF | free | `ft81x.h` |

Bitmap handles: 1 image, 2 Games, 3–7 the five font roles, 8 icons, 15
coprocessor scratch (`hal_api.h`). SIDTracker has no Games or cover, so those
bands are free for its own bitmaps (e.g. scopes, waveform tiles).

## Fonts

Generated Roboto / Roboto Mono L4 (4-bit anti-aliased) FT81x legacy fonts in
`src/core/ui/fonts/font_data.inc` (`tools/fonts/gen_fonts.py`), uploaded once
by `init()` and bound per frame by `bindFontSet()`. Without a verified upload
the UI falls back to the FT81x ROM fonts 16–31. Five font roles per set
(spec 20 §2.1). `ft81x.h` includes `ui/fonts/fonts.h`, so the driver depends on
the font blob (APPROACH §1 lists cutting that dependency).

## Simulator

`sim/src/sw_display.*` interprets the same `IDisplay` subset in software
(blending, scissor, primitives, ROM-font approximations, custom fonts from the
same blob); window shown upright (no 180° rotate). It does not model RAM_DL
size limits or coprocessor timing (`sim/README.md` "HAL mapping",
"Limitations").

## Cost per frame

Not measured in the sources. Facts: a full list is at most 2048 words (8 KB);
each command byte costs one polled SPI transfer at 30 MHz (≥ 0.27 µs, more
with polling overhead), so a large frame is a few ms of main-loop time
(estimate). The device telemetry (`telemetry: ui N frames ... render avg/max
ms, last frame W DL words`, `app_main.cpp`) gives the real number on hardware;
record it at M0.

## Unknowns

- Exact FT81x part and whether a crystal is fitted (spec 01 §12.3; `CLKEXT` fallback exists).
- RAM_DL overflow behaviour is not handled by the driver (no check against 2048 words).
- `CMD_SNAPSHOT2` orientation under `REG_ROTATE` and whether it needs the scan-out stopped (`ft81x.cpp` UNVERIFIED, `kSnapshotFlipped`).
- Per-frame SPI time on hardware: pending M0 telemetry.

## SIDTracker display-list budget

The driver does not check RAM_DL overflow, so SIDTracker keeps every frame
well under 2048 words. Dense views (pattern grid, table editors) draw ROM
font 18 glyphs as one `VERTEX2II` each inside one `BEGIN(BITMAPS)` per
colour (`ui::GlyphLayer`, `src/ui/screen.h`) instead of a `CMD_TEXT` per
field. `tests/app_test.cpp` (`display_list_fits_with_a_full_pattern`)
estimates the words of every screen and page with a conservative
`CMD_TEXT` cost and fails above 1800: worst case 1,322 (full pattern).
The real count should be confirmed on the device (`REG_CMD_DL` after a
full-pattern frame; `docs/hardware-test-plan.md` 5.4).
