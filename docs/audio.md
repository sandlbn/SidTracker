# Audio path

Plan §21 Task 2: application → audio buffers → DMA → codec, with buffer
size, sample rate, timing budget and the measured baseline. Sources:
OpenTracker (`ReversedPolytracker-1.9x`, branch `opentracker`, HEAD
`0849cb0`). Driver `reimpl/src/hal/audio_sgtl5000.*`, reused as
`src/platform/audio/audio_sgtl5000.*`.

## Chain

```
SID engine / mixer  ──FillFn(out, 128, ctx)──►  txBuf idle half (int16 L,R interleaved, 512 B)
  (I2S TX DMA ISR, NVIC 0x50)                        │
                                                     ▼
                     eDMA TX channel: 16-bit, SOFF 2, 512 majors, circular (SLAST −1024),
                     INTHALF | INTMAJOR, DMAMUX source 13 (I2S0_TX)
                                                     │  to TDR0 + 2 (upper half of the 32-bit slot)
                                                     ▼
                     I2S0 (SAI) master: 2 × 32-bit slots per frame, BCLK 64·Fs, FIFO watermark 1, FCONT
                                                     │  PTE10 TXD, PTE11 FS, PTE12 BCLK, PTE6 MCLK
                                                     ▼
                     SGTL5000 (I2C0 0x0A), slave: I2S_IN → DAP → DAC → headphone / line out
```

Sources: `audio_sgtl5000.cpp txIsr()`, `dmaStart()`, `i2sConfig()`; spec 01
§5.2–§5.3; spec 04 §1.1.

| Step | Detail | Source |
|---|---|---|
| Contract | `IAudioOut::FillFn(int16_t* out, uint32_t frames, void* ctx)`: fill exactly 128 interleaved L/R frames, no blocking, no allocation; called from the DMA ISR | `hal_api.h` |
| Callback install | `setCallback(fn, ctx)` swaps the pointer with IRQs off; `start()` sets running and unmutes (PTD6 HIGH); `stop()` mutes; while stopped the ISR zero-fills | `audio_sgtl5000.cpp` |
| Buffer | `txBuf[2 × 128 × 2]` int16 = 1,024 B, 32-byte aligned, static `.bss` (no `.dmabuffers` section); OpenTracker build places it at 0x20022A20 (SRAM_U) | `audio_sgtl5000.cpp`; `nm` on `build/opentracker.elf` |
| Half selection | ISR reads the TX TCD `SADDR`; if DMA is in the first half, the second half is filled, and vice versa | `txIsr()` |
| Output latency | 1–2 blocks (2.9–5.8 ms) from fill to DAC (derived from the double buffer) | derived |
| DMA | channel allocated by `DMAChannel::begin` (TX first → ch 0, RX → ch 1, derived) | `dmaStart()`; spec 01 §12.7 |
| I2S | MCR 0x40000000, MDR 0xF0FE, TCR2 0x07000001, TCR4 0x00011F1B \| FCONT, TCR5 0x1F1F1F00 | `board.h`; spec 01 §5.2 |
| Codec | enable table of 16 writes (spec 01 §5.1), then DAP init (SSS 0x70, 5-band GEQ, bass at minimum); line-out level fixed 0x1D1D; headphone left **muted** (HP_CTRL 0x7F7F) | `Sgtl5000Audio::init()`; `audio_sgtl5000_dap.h` |
| Volume | `setHeadphoneVolume(0..1)` must be called by the app (OpenTracker app 0.7, selftest 0.5), else headphones stay silent | `app_main.cpp` "Field: no sound on headphones" |

Line input (kept, APPROACH §1): RX DMA `RDR0+2` → `rxBuf` (same shape),
DMAMUX 12, ISR priority 0x40 records the completed half; `lineInBlock()`
copies 512 B. SIDTracker does not need the input but keeps RX running because
its start order is part of the proven sequence.

## Bring-up order (field-proven, keep exactly)

1. Mute PTD6 LOW (spec 01 §5.4).
2. `i2sConfig()`: clock gates; write `MCR`, wait until `MCR.DUF` (bit 31) clears, **then** `MDR`, wait again. Writing MDR while DUF was set left MCLK at 180 MHz (blocks 15.94× too fast, codec silent on I2C).
3. `dmaStart()`: **TX before RX** (RX is synchronous to TX; RX first left TX stuck with FEF set); `TCR4.FCONT` set so TX keeps running after a FIFO underrun; FEF cleared once.
4. 5 ms, `audioProbe()` (logs blocks/s, expect ~345; DMA channel numbers; I2C0 scan), codec CHIP_ID read (diagnostic), enable table, 400 ms after `CHIP_DIG_POWER`.
5. Si4703 pins stay unconfigured until after the codec (spec 08 §1, §5).

Sources: `audio_sgtl5000.cpp` comments; spec 08 §1–§4; APPROACH §1.

## Sample rate and budget

| Quantity | Value | Derivation / source |
|---|---|---|
| MCLK | 180 MHz × 16/255 = 11.294 MHz | spec 01 §5.2; `kI2sMdr` |
| Fs (device) | MCLK / 256 = **44,117.647 Hz** | `board.h kSampleRateMilliHz = 44117647` |
| Fs (nominal, `IAudioOut::kSampleRate`, simulator) | 44,100 Hz; device is 0.04 % (≈ 0.7 cent) high | `hal_api.h`; `sim/README.md` "Limitations" |
| Block | **128 frames** | `board.h kAudioBlockFrames`; `IAudioOut::kBlockFrames` |
| Block period | 128 / 44,117.647 = **2.9013 ms**; ≈ 344.7 TX interrupts/s | derived |
| Budget | 180 MHz × 2.9013 ms = **522,240 cycles/block** (exact: 128 × 255 × 256 / 16) | `telemetry.h kRenderBudgetCycles` |
| Alarm | 70 % = 365,568 cycles (`RENDER>70%` in telemetry) | `telemetry.h kRenderAlarmPermille` |
| SID clocks per sample (PAL 985,248 Hz) | 22.332 at 44,117.6 Hz; 22.341 at 44,100 Hz | derived; APPROACH §2 uses 22.33 |

The tick clock must count audio samples at the device Fs and the pitch table
must be computed for 44,117.6 Hz (APPROACH §7 M1); the simulator runs at
44,100 Hz.

## ISR priorities and preemption

NVIC (lower = more urgent): SysTick 32, I2S RX DMA 0x40, UART4 status
(MIDI DIN) 64, **I2S TX DMA 0x50 (render)**, USB 112, diag drain 112, others
128 (`board.h`, `teensy3/serial5.c`, `usb_dev.c`, `mk20dx128.c`,
`hal/diag.cpp`). The render can be preempted by SysTick, the RX ISR and the
MIDI UART ISR (all short); it preempts USB and the whole main loop (UI, SD,
I2C, display). So render load directly steals main-loop time; I2C waits are
written to survive this (`board.h kI2cMinPolls`).

## Cycle measurement

`txIsr()` reads `ARM_DWT_CYCCNT` around the callback only (not ISR entry,
half selection or bookkeeping). The count is wall time inside the callback,
so it includes any higher-priority ISR that preempted it. Two statistic sets:
lifetime `renderCycles(min, avg, max)` and a window `renderCyclesWindow()`
that `app_main.cpp` drains every 1 s into the 5 s telemetry line and the
`/.develop` overlay. `blocksRendered()` counts every TX interrupt (also when
stopped).

## Underruns

OpenTracker has **no underrun counter**. The TX TCD is circular, so a callback that
runs past the end of its half lets the DMA play the half being written (or
stale data) with no flag; `FCONT` keeps the SAI transmitting, and `TCSR.FEF`
is cleared once at start and never read. Only `max` cycles > budget hints at
it. APPROACH §1 / §7 make an underrun counter a local M0 change. Candidate
checks (proposal, not in the sources): compare the callback cycles with the
budget; after the callback re-read `SADDR` and test that the DMA is still in
the other half; test whether the channel's `DMA_INT` bit is set again before
the ISR returns; sample `TCSR.FEF` (whether it latches with FCONT set is
UNVERIFIED).

**SIDTracker (local change 1, `src/platform/PROVENANCE.md`)** implements the
`SADDR` re-read: after the fill callback the TX ISR reads the DMA source
address again, and if the DMA is already inside the half that was just
written the block counts as late (`Sgtl5000Audio::underruns()`, shown on the
benchmark page). UNVERIFIED on hardware: the first M0 session provokes a
late block (busy-wait in the callback) and checks the count moves.

## Existing figures (OpenTracker, not SIDTracker)

| Figure | Source |
|---|---|
| audio ISR "56–72 % CPU on hardware" (earlier build, justification for the I2C poll rule) | `board.h` comment |
| about 40 % (≈ 209k cycles) for 8 voices + reverb + delay | `docs/NEW_HARDWARE_PORTING.md` §3 |
| idle 7 %, 8 sample voices with delay and reverb 37–48 % | APPROACH §2 (field telemetry; not in OpenTracker docs) |

Bench tools: `tests/audio_bench.cpp` (`make -C tests bench`, host wall time)
and `bench-m4` (device flags under QEMU `mps2-an386 -icount`; counts executed
M4 instructions, not cycles: no flash wait states or SDRAM latency)
(`tests/Makefile`). The selftest has a ready test oscillator: 256-entry sine
table, 440 Hz for 3 s, then the pitch of the lowest held pad (`selftest.cpp
toneFill`).

## Measured baseline

SIDTracker M0 benchmark page (plan §6), silent callback and test oscillator.

| Field | Derived | Measured |
|---|---|---|
| Audio buffer period | 2.9013 ms (128 frames at 44,117.6 Hz) | pending hardware run (M0) |
| Cycles available | 522,240 per block | pending hardware run (M0) |
| Cycles consumed | — | pending hardware run (M0) |
| Peak cycles | — | pending hardware run (M0) |
| CPU utilization | — | pending hardware run (M0) |
| Audio underruns | — (no counter yet) | pending hardware run (M0) |

## SID engine estimate vs measurement

APPROACH §2 estimated ≈ 400 cycles per output sample (≈ 53k per block, 10 %).
The engine as built (`src/sid`, `src/audio`, `src/app/engine.cpp`) was
measured two ways; the K66 column is filled in by the first hardware run
(benchmark page, Shift+Master since 0.6.2; procedure in `docs/hardware-test-plan.md`).

`make -C tests bench-m4` builds `tests/engine_bench.cpp` with the device
compiler flags for Cortex-M4F and runs it under QEMU `mps2-an386 -icount`.
The numbers are **executed instructions** per 128-frame block, a floor for
M4 cycles (no flash wait states, FPU or load-use stalls), 2026-10-16:

| Section | idle (no notes) | six voices, tables, both filters (avg / peak) | stress (avg) | estimate | K66 cycles |
|---|---|---|---|---|---|
| SID1 voices | 41.7k | 59.0k / 94.4k | 55.3k | 19.2k | pending |
| SID1 filter | 4.4k | 4.4k | 4.4k | 5.1k | pending |
| SID2 voices | 41.7k | 59.0k / 94.6k | 55.5k | 19.2k | pending |
| SID2 filter | 4.4k | 4.4k | 4.4k | 5.1k | pending |
| mixer | 6.6k | 6.6k | 6.6k | 2.6k | pending |
| player | 0.2k | 0.4k / 5.1k | 0 | 2k | pending |
| **total** | **99k (19 %)** | **133k (25.5 %) / 205k (39 %)** | **126k (24 %)** | 53k (10 %) | pending |

Reading: filter and player cost about what was estimated; voices cost
about 3x the estimate (≈ 150 instructions per voice-sample playing, ≈ 110
idle). The peaks are note starts and hard restarts, where rate-0 envelopes
take two or three steps per sample. Already done after the first
measurement: envelope fast paths (released / sustaining / attacking without
per-hit loops) and inlining the oscillator into the chip loop (−30 %).
Next candidates, to be decided on K66 numbers only (PLAN.md §6, §20): hot
code in RAM (`FASTRUN`), closed-form decay/release steps, skipping silent
voices' waveform work. At the pessimistic CPI of 1.5 the stress case is
≈ 190k cycles (36 %), under the 70 % alarm with room for drums and one
effect, not the 10 % hoped for.

**Extended engine (M8)**, same benchmark, 2026-10-16: six voices with
tables and both filters, plus delay + chorus + reverb at 40 %, a drum on
every row, both LFOs and per-voice pan on every voice:

| Section | avg | peak |
|---|---|---|
| SID voices (both) | 123.8k | 193.6k |
| SID filters (both, incl. panned buffers) | 12.7k | 12.8k |
| mixer (incl. two panned paths) | 14.5k | 14.5k |
| PCM (four voices at most) | 8.6k | 14.0k |
| effects | 77.0k (14.7 %) | 77.1k |
| **total** | **238k (45.5 %)** | **307k (59 %)** |

Effects are the largest addition; the delay line is in SDRAM (its latency
is not in an instruction count), chorus and reverb lines in SRAM. With
effects off (mix 0 or the global switch) the M8 cost is the mixer's panned
path and, while drums sound, PCM.

**First K66 numbers** (0.4.0, Master page "CPU utilization", owner's
reading, 2026-10-16; the per-section table of that build still showed PCM
and effects as "not built", fixed in 0.4.1):

| Load | K66 avg | K66 peak | QEMU insn avg / peak | cycles per insn |
|---|---|---|---|---|
| song playing, effects off | 37 % | 69 % | 27 % / 40 % | ≈ 1.4 / 1.7 |
| stress (F1) | 39 % | – | 26 % | ≈ 1.5 |
| song playing, all effects on | 68 % | ≈ 68 % | 45.5 % / 59 % | ≈ 1.5 |

The effects cost ≈ 31 points (≈ 160k cycles) on the K66, twice their
instruction count, and leave almost no room under the 70 % alarm.
Per effect (QEMU insn, 0.4.0): delay 6.8k, chorus 15.7k, reverb 54.7k.

**0.4.1:** the reverb runs at half rate on float lines (same 24 KB of SRAM):
20.3k instructions instead of 54.7k; all three effects 42.6k instead of
77.0k; the full M8 scenario 203k avg (38.9 %) / 273k peak (52 %) in
QEMU. K66 numbers for 0.4.1 pending.

Host (`make -C tests bench`, x86-64 -O2): ≈ 11–14 µs per 2.9 ms block.

Reference points (APPROACH §2): cycle-exact at ~1 MHz ≈ 346k/block ≈ 66 %;
2× oversampled fallback ≈ 20 % (40 % pessimistic).

## Unknowns

- DMA channel numbers at runtime (derived 0 = TX, 1 = RX; spec 01 §12.7). Boot log `probe ... dma tx ch` settles it.
- Codec `CHIP_I2S_CTRL 0x0130` (32·Fs) on a 64·Fs bus works but is unexplained (spec 01 §5.2 [I]).
- Mute polarity of PTD6 (`board.h` UNVERIFIED [I]); the field shows audio after `start()`.
- Whether `TCSR.FEF` is a usable underrun indicator with FCONT set.
- Placement of `txBuf`/`rxBuf` is up to the linker; behaviour if they land in SRAM_L behind the LMEM cache is unverified (`sdhc.cpp` notes the same question).
- DAP stage: OpenTracker puts the DAP in the DAC path (SSS 0x70). SIDTracker keeps the driver unchanged for now (PROVENANCE.md "Kept although unused"; it never calls `applyMasterDsp`); dropping the DAP (stock SSS 0x0010) is deferred until it is measured.
- Stock headphone level unknown (README "Differences": fixed 0.7).
