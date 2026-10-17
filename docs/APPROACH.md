# Approach: mapping the plan onto the OpenTracker platform

Companion to [PLAN.md](PLAN.md), which owns scope. This records the decisions
the plan leaves open, with the numbers behind them. Every CPU figure is an
estimate until the K66 has measured it (plan §6, §20).

## 1. Platform reuse: vendored copy with provenance

OpenTracker, our clean-room firmware at `ReversedPolytracker-1.9x/reimpl`
(branch `opentracker`, commit `c69d753a`), already runs display, SDRAM, SD,
keypads, pad LEDs, encoder, codec, MIDI and USB on the real device. Its
`src/hal` (about 5,800 lines) implements the interfaces in `src/core/hal_api.h`;
`src/core` never touches a register.

| Option | Verdict |
|---|---|
| **Vendored copy + `PROVENANCE.md`** (chosen) | The HAL is small, we will change it (drop radio and codec DAP, add an underrun counter and a raw-detent mode, cut `ft81x.h`'s dependency on the UI font blob), and the `opentracker` branch is a frozen stock-faithful line that must not be refactored for us. `tools/sync_hal.sh` diffs each vendored file against the recorded commit; fixes flow both ways by hand. |
| git subtree | Not one prefix: `src/hal` needs `src/core/hal_api.h`, `src/core/ui/fonts`, `third_party/teensy3` and `third_party/fatfs`. Four subtrees with local edits are worse than a diff script. Fallback if the owner wants automatic pulls. |
| Shared library repo | Right later, wrong now: it forces a refactor of the frozen line before we know which parts stay identical. Revisit after M3 if the vendored diff is still near zero. |

Import set: `src/core/hal_api.h`, `src/hal/*`, `src/core/ui/fonts/*`,
`third_party/{teensy3,fatfs}`, `tools/check_image.py`, `sim/src/*`,
`tests/{test.h,mem_storage.h,ui_fakes.h,bench_m4/*}`. Field-proven facts travel
with it unchanged: write `I2S0_MDR` only after `MCR.DUF` clears (else MCLK runs
at 180 MHz and the codec NAKs); start the I2S transmitter before the
synchronous receiver, with `TCR4.FCONT`; enable an I2C clock gate before its
registers; the FTM2 encoder init is the exact write sequence in
`PanelInput::init` (spec 09 §2) with its ≥100 ms poll, 4 counts per detent and
overflow-band discard; I2C timeouts need 1 ms and ~2,000 polls because the
audio ISR preempts the wait loop.

**Boundaries, kept.** `IDisplay`: FT81x display-list builder; screens drawn
through it run unchanged in the simulator; we reuse OpenTracker's `ui/gfx`
primitives and generated Roboto fonts, nothing else of its UI. `IInput`: queued
button, pad and encoder events with the stock acceleration applied; local
change: a raw-detent flag for the jog wheel as value editor and for Perform.
`IAudioOut`: pull callback filling 128 stereo int16 frames from the DMA ISR, no
allocation or blocking; the SID mixer renders straight into it; the line-in RX
DMA keeps running because its start order is part of the proven sequence;
local change: an underrun counter (plan §6). `IStorage`, `IMidiPort`, `ILeds`,
`IClock`, `INvStore`: unchanged. `IRadio` dropped; the Si4703 stays parked in
reset as `board.cpp` does.

**Simulator and host tests.** `sim/` builds the same core against SDL2 host
HALs: software FT81x renderer, 44,100 Hz audio, a folder as SD card, scripted
headless runs with virtual time, `--screenshot` and `--wav`, so DSP regression
(plan §18) and UI tests need no hardware. Host tests follow `tests/Makefile`
(ASan/UBSan); `bench-m4` runs the engine under QEMU `mps2-an386 -icount` for
M4 instruction counts before a flash. Gates per task: device build with image
check, host tests, simulator build, sim smoke script.

**Update and recovery, kept exactly.** Images are Intel HEX named
`PolyendTrac*.ptf`, exactly 24 characters (the bootloader ignores longer names
and then flashes a random file): `PolyendTrackerSTv010.ptf` for 0.1.0, enforced
by the Makefile. Every image passes `tools/check_image.py` (FSEC 0xDE / FOPT
0xF9 at 0x400, nothing past the stock extent 0xD6460, SP in SRAM, record types
00/01/02). `main()` runs `boardEarlyInit` (rail and SD only) and
`checkRestoreTrigger` before any other driver: `/Firmware/ot_restore.txt` names
the image, is renamed `.done`, and `requestFirmwareUpdate` writes
`/Firmware/_fwinfo`, sets EEPROM byte 0 = 1 and pulls PTA1 low (the only code
allowed to). Settings gets the same "Firmware update" picker. SD rule: stock
1.9.2 plus one test image in `/Firmware`, the rest in `/OT_hold`, because the
rear reset button installs a random `.ptf`. Every milestone's first hardware
session proves the way back before testing anything else.

## 2. Budgets

From the code (`board.h`, `telemetry.h`): Fs = MCLK/256 = **44,117.6 Hz**
(nominal `IAudioOut::kSampleRate` 44,100; 0.7 cent high, compensated in the
pitch table), block **128 frames** (2.90 ms), budget **522,240 cycles/block** at
180 MHz, alarm at 70 % (365k). OpenTracker idles at 7 % and plays 8 sample
voices with delay and reverb at 37–48 %. Flash: 1.9.4 uses 594 KB of the
877 KB ceiling; SRAM static 209 KB of 256 KB. SIDTracker has no sample engine,
so SRAM is far freer: SID state and tables (about 40 KB) live in SRAM; the
16 MB SDRAM (slow random access) holds patterns, undo, projects and later PCM.

Estimate per output sample at 44.1 kHz (PAL SID clock 985,248 Hz = 22.33 SID
clocks per sample):

| Item | cycles/sample | × | cycles/block | % budget |
|---|---|---|---|---|
| Voice: 24-bit accumulator with sync/ring/test, waveform + polyBLEP, LFSR noise edges, combined-wave lookup, ADSR in SID-clock units | 50 (40–80) | 6 | 38.4k | 7.4 |
| Filter: SVF with 6581/8580 cutoff curves and 6581 saturation, mode mix | 40 (30–60) | 2 | 10.2k | 2.0 |
| Mixer, master volume, SID balance, stereo out | 20 | 1 | 2.6k | 0.5 |
| Per block: tick clock, tables, register-write queue | | | 2k | 0.4 |
| **Total** | **≈ 400** | | **≈ 53k** | **≈ 10 % (20 % at 2× pessimism)** |

Cycle-exact at ~1 MHz for comparison: 22.33 × (6 × 12 + 2 × 25) ≈ 2,700
cycles/sample ≈ 346k/block ≈ 66 % before decimation, so it trips the alarm with
nothing else running. The 2× oversampled fallback doubles the voice and filter
rows (≈ 20 %, 40 % pessimistic) and still fits. M0 measures the platform
baseline with a silent callback; M1–M3 replace every row with DWT numbers.

## 3. SID engine

**Own fixed-point implementation, per-sample at 44.1 kHz with band-limited
oscillators**; cycle-exact is rejected for the device by the numbers above.
Semantics stay SID: the engine is driven by register writes (`write(reg, val)`
with a sample offset inside the block); the 24-bit accumulator steps by
`freq × 22.33` per sample so sync, ring modulation and the noise LFSR (clocked
on bit-19 edges, counted per step) keep chip behaviour; ADSR rate and
exponential counters run in SID-clock units with fractional accumulation. Saw
and pulse get polyBLEP edges; triangle is left alone. Fallback when pulse-width
sweeps or combined waveforms alias audibly: 2× oversampling with a half-band
decimator, decided per section by measurement.

- Combined waveforms: our own bitwise-AND plus neighbour-bit attenuation model,
  one generated table per combination and chip (about 32 KB), tuned against
  reference captures; measured tables exist only in GPL code.
- 6581 vs 8580 per chip: 8580 first (near-linear SVF, documented cutoff
  curve), 6581 as a parameterised curve plus a cheap saturation stage. Mixed
  configurations are two instances with different parameters.
- reSID / reSIDfp are GPL-2.0-or-later: linking either makes the whole firmware
  GPL, and the vendored PJRC core carries an extra device-list clause whose GPL
  compatibility is unresolved. Recommendation: no port. Use reSIDfp only as a
  **separate host-side oracle executable** under `tools/oracle/` (built from
  source the developer fetches, never linked) to produce reference WAVs for
  `tests/audio/reference/`; program output is not covered by the GPL.

## 4. GoatTracker

GoatTracker 2 is GPL: its player and editor code are never copied;
interoperability through the documented `.sng` and `.ins` formats is fine.
`goattracker-format.md` is written from the GT2 `readme.txt` format and command
sections (version recorded) and byte-level checks of files produced with GT2
itself. Fixtures: our own songs and instruments in `tests/data/goattracker/`
with JSON expectations of the parsed structure; songs we cannot redistribute in
`fixtures/private/` (git-ignored, tests skip when absent); import → export →
byte-equal round trips where GT2 semantics allow; the plan §8 compatibility
report asserted per fixture.

## 5. Licence

**MIT for SIDTracker's own code**; third-party components keep their licences
(PJRC core MIT-style, FatFs BSD-style, Roboto Apache 2.0 / OFL). It matches the
permissive platform, keeps the PJRC clause moot and keeps a later permissive
HAL library possible. Consequence: no reSID and no GoatTracker code, ever; GPL
tools stay out of process. Alternative: GPL-3.0-or-later as in CommanderSID,
which allows a reSID port but makes the PJRC clause and future permissive reuse
a problem. Owner input needed: OpenTracker has **no LICENSE file** today, and
the vendored HAL needs one before SIDTracker can state its own; the USB identity
`1209:0001` is the pid.codes test PID and needs a real registration.

## 6. Repository layout

The plan's tree, with the vendored HAL in `platform/` under its upstream file
names so `sync_hal.sh` can diff them:

```
src/platform/            hal_api.h, board.*, k66.h, hw_math.h, i2c_bus.*, diag*,
                         fw_update.*, midi_ports.*, selftest.*, PROVENANCE.md
  display/               ft81x.*, fonts/
  input/                 input.* (keypads, FTM2 encoder), pad_leds.*
  audio/                 audio_sgtl5000.*
  storage/               sdhc.*, sd_storage.*, nvstore_eeprom.*
  timer/                 SystemClock (from board.cpp), DWT helpers, telemetry.*
src/tracker/{song,pattern,playback,commands}   model, tick clock, command decode
src/sid/                 sid.cpp (register file, write queue), voice, oscillator,
                         envelope, filter, tables (generated)
src/compatibility/goat/  import, export, instrument, commands
src/ui/{pattern,instrument,perform,song,settings}   screens over IDisplay + gfx
src/audio/               mixer.cpp (6 voices, 2 filters, balance); pcm, effects later
src/app/                 main.cpp (boot, restore check, HalSet wiring, telemetry)
third_party/{teensy3,fatfs}   sim/   tests/   tools/{check_image.py,sync_hal.sh}
```

## 7. Milestones M0–M3

Plan §21 Tasks 1–3 are M0, Task 4 is M1, Tasks 5–6 are M2, Task 7 is M3.

**M0 Platform bring-up.** Vendor the import set with `PROVENANCE.md`; Makefile
with the 24-character name and image check; the Task 1 docs and `audio.md`
(Task 2); app skeleton: boot screen, on-screen input log, test oscillator
(pads → note, encoder → pitch), SD text read, benchmark page with the plan §6
fields. Host: tests build against the fakes; a headless sim script yields a
screenshot and a `--wav` whose FFT peak is the expected tone. Hardware: flashed
from stock Config > Firmware update; every button, pad and detent shows; tone on
headphones and line out; 10 minutes without underruns; `ot_restore.txt` back to
stock, then the Settings picker back again.

**M1 One SID voice.** Oscillator, envelope, register interface, pitch table
compensated for 44,117.6 Hz; one-channel `NOTE | INST | CMD` playback with a
tick clock counted in audio samples, never UI frames. Host: accumulator after N
samples equals the SID formula; saw/pulse fundamental and duty by FFT; ADSR
transition times per rate within one sample; deterministic render hash.
Hardware: A-4 within 1 cent, ADSR audible, cycles per voice on the benchmark
page, no underruns.

**M2 One complete SID.** Three voices, hard sync, ring modulation, noise, pulse
width, filter with both models, per-section DWT profiling. Host: LFSR sequence
(taps 22/17); sync and ring against a slow reference in the test; cutoff and
resonance sweeps; 3-voice fixture hash; `bench-m4` count. Hardware: 30 minutes
of a worst-case pattern under the 70 % alarm; numbers recorded in `audio.md`
beside the estimate.

**M3 Dual SID.** Second instance, independent models, six-voice mixer with
balance, Settings page. Host: two instances with different models differ in
filter output; 6-voice fixture hash. Hardware: six voices, two filters, all
noise and filters on, 30 minutes without underruns; model switch without clicks.

**Risks.** Aliasing forces 2× oversampling and halves the headroom. Combined
waveforms and the 6581 filter sound wrong without measured tables: tune against
captures, accept "8580 accurate, 6581 plausible" for 0.1. The 0.04 % Fs offset
between device and simulator: fixtures rate-nominal, pitch compensated on the
device. Encoder poll and acceleration were designed for stock menus: raw-detent
flag. Rear reset picks a random image: keep `/Firmware` clean. Late-block
underruns are invisible today: counter in M0. GoatTracker details documented
only in GPL source: the owner decides whether reading it for format facts is
acceptable; the plan relies on the readme and produced files.

## 8. Commander X16 interop

`~/Projects/CommanderSID` (GPL-3.0-or-later) drives a real SID from a Pico and
queues writes as `{reg, value, timestamp}`. SIDTracker's "Event / Register
Stream" layer emits exactly that. Shared opportunity: a documented, licence-free
**SID register-write stream format** (chip, register, value, cycle timestamp)
that SIDTracker exports, the X16 card plays, and a future `.sid` import
produces. Share the specification, not code.
