# Hardware

Plan §3: MCU, clocks, memory map, timer and DMA resources, RAM left after
the required services, pins, buses. Sources are in OpenTracker
(`ReversedPolytracker-1.9x`, branch `opentracker`, HEAD `0849cb0`; see
[current-state.md](current-state.md)). Constants live in
`reimpl/src/hal/board.h`, reused as `src/platform/board.h`; every line there
cites spec 01.

## MCU and clocks

| Item | Value | Source |
|---|---|---|
| MCU | NXP MK66FX1M0 (Cortex-M4F, single-precision FPU), Teensy 3.6-style runtime | spec 01 §0 |
| Crystal | 16 MHz | spec 01 §0, §11 |
| Core | 180 MHz, HSRUN (PLL: 8 MHz ref × 45 / 2) | spec 01 §1 steps 11–13; `board.h` `kCoreHz` |
| Bus | 90 MHz (`SIM_CLKDIV1 = 0x01060000`, checked at boot as `clocksOk`) | spec 01 §1 step 14; `board.cpp` |
| FlexBus / SDRAM | 90 MHz (OUTDIV3 = 1, set in SDRAM init) | spec 01 §2 step 1 |
| Flash clock | 25.7 MHz | spec 01 §1 step 14 |
| USB | IRC48M with clock recovery | spec 01 §1, §9 |
| I2S MCLK | 180 × 16/255 = 11.294 MHz → Fs = 44,117.6 Hz | spec 01 §5.2; [audio.md](audio.md) |

Pitfall: the Teensy `eeprom_write_byte()` leaves HSRUN and its re-entry
rewrites `SIM_CLKDIV1` with OUTDIV3 = 0 (SDRAM at 180 MHz). `fw_update.cpp`
and `nvstore_eeprom.cpp` restore OUTDIV3 = 1 with IRQs off after each write.

## Memory map

| Region | Range | Size | Notes | Source |
|---|---|---|---|---|
| Program flash | 0x0000_0000 | 1 MB (linker) | vectors at 0, flash config 0x400–0x40F; images limited to < 0xD6460 (877,664 B) | `mk66fx1m0.ld`; `tools/check_image.py` |
| FlexRAM (EEPROM emulation) | 0x1400_0000 | 4 KiB window | byte 0 = update/power flag, app area from byte 10 | spec 02 §9; `nvstore_eeprom.cpp` |
| SRAM_L | 0x1FFF_0000–0x1FFF_FFFF | 64 KB | code bus; LMEM cache considerations for DMA | `sdhc.h` `detail::kSramLBase`; `sdhc.cpp` |
| SRAM_U | 0x2000_0000–0x2002_FFFF | 192 KB | system bus; `_estack` = 0x2003_0000 | `sdhc.h`; `mk66fx1m0.ld` |
| SDRAM | 0x7000_0000–0x70FF_FFFF | 16 MB | 16-bit FlexBus SDRAMC, CL3, CTRL 0x0256, AC0 0x70003320, CM0 0x00FC0001; slow random access | spec 01 §2; `board.h`, `board.cpp sdramInit()` |

A DMA burst must not cross the SRAM_L/SRAM_U boundary (`sdhc.h`
`dmaDirectOk`). SRAM layout per the PJRC linker script: `.usbdescriptortable`,
`.dmabuffers`, `.usbbuffers`, `.data`, `.noinit`, `.bss`, then heap upward and
stack downward from 0x2003_0000 (`mk66fx1m0.ld`). Size of the FlexNVM behind
the EEPROM emulation is not recorded in the sources (only "4 KiB FlexRAM
window", spec 02 §9).

## RAM and flash after required services

Measured with `arm-none-eabi-size` and `nm` on `reimpl/build/opentracker.elf`
(full OpenTracker 1.9.4 app; built 2026-10-16 09:06 from the tree committed
as `3b60137`, same code as HEAD). SIDTracker must re-measure its own ELF.

| Item | Bytes | How |
|---|---|---|
| `.text` + `.data` (flash used) | 594,256 | `size`: text 586,964, data 7,292 |
| Flash headroom to the image limit | 283,408 | 877,664 − 594,256 |
| Static SRAM (0x1FFF0000.._ebss 0x20023100) | 209,152 | `nm`: `_ebss` |
| Left for heap + stack | 52,992 | 0x20030000 − `_ebss` |
| of which OpenTracker `App` object | 85,088 | `nm`: `gApp` |
| HAL static SRAM (`ot::hal` symbols) | 43,105 | `nm` sum; largest: diag log 14,376, SD bounce 8,192, 14 FatFs `FIL` 7,728, fault text 4,096, MIDI rings 2,088, audio TX/RX rings + copy 2,560 |
| Teensy core, newlib, FatFs, USB static | ≈ 6.8 K | `nm` non-`ot::` minus `gApp` |
| HAL code | ≈ 36 K | `nm` `ot::hal` text |
| UI font blob (flash, uploaded to RAM_G) | 129,248 | `font_data.h` `kBlobSize` |

Estimate for SIDTracker: the platform needs about **50 KB of SRAM** statically
(HAL + core), leaving roughly **200 KB** for SID state, UI and stack. The stack
size actually used is not measured anywhere in the sources. Flash: platform
(≈ 36 K HAL + ≤ 60 K core/libs + 129 K fonts ≈ 225 K) leaves about 650 KB
under the 877,664-byte limit. These are estimates from the OpenTracker symbol
table, not a SIDTracker build.

## Timer resources

| Timer | Use | Source |
|---|---|---|
| SysTick | 1 ms tick (`millis`), priority 32; wrapped by `diagSystickEntry` (10 ms stuck watchdog, log drain fallback) | spec 01 §1 step 18; `teensy3/mk20dx128.c` (SHPR3); `hal/diag.cpp` |
| DWT CYCCNT | enabled in `diagInit()` and boot step 6; audio render statistics, `softOff()` busy-waits | `hal/diag.cpp`, `hal/board.cpp`, `hal/audio_sgtl5000.cpp` |
| FTM2 | quadrature decoder for the encoder, no clock (SC = 0), no IRQ | `hal/input.cpp`; spec 09 §2–§3 |
| FTM0/1/3, TPM1 | started with Teensy PWM defaults by the core; not used by the HAL | `teensy3/pins_teensy.c` |
| PIT, LPTMR, PDB | unused (no reference in `reimpl/src/hal`) | grep; spec 01 §10 (stock: PIT never started) |

Free for SIDTracker: PIT0–3, LPTMR, FTM0/1/3. Tick timing in the plan comes
from the audio sample count, not a timer (APPROACH §7 M1).

## DMA and interrupt resources

eDMA has 32 channels on this part (`kinetis.h` `DMA_NUM_CHANNELS`), but the
PJRC `DMAChannel` allocator hands out only 0–15, lowest free first
(`DMAChannel.cpp`). Allocation in OpenTracker:

| Channel (derived) | User | DMAMUX source | ISR priority | Source |
|---|---|---|---|---|
| 0 | I2S0 TX ring → `TDR0+2` | 13 (I2S0_TX) | 0x50 | `audio_sgtl5000.cpp dmaStart()` (TX `begin()` first) |
| 1 | `RDR0+2` → I2S0 RX ring | 12 (I2S0_RX) | 0x40 | `rxDmaStart()` |
| — | SDHC uses its own ADMA2 engine, not eDMA; polled, no IRQ | — | — | `hal/sdhc.cpp` |
| — | FT81x SPI2 is polled byte by byte (stock used an SPI DMA channel only for the NES frame) | — | — | `hal/ft81x.cpp`; spec 01 §4.3 |

Channel numbers are inferred from allocation order (spec 01 §12.7); the boot
log `probe ... dma tx ch N` in `audioProbe()` prints the real ones.

NVIC priorities in use (lower = more urgent): SysTick 32; I2S RX DMA 0x40 (64);
UART4 status (MIDI DIN) 64 (`teensy3/serial5.c`); I2S TX DMA 0x50 (80, the
audio render); USB 112 and the diag drain software IRQ 112 (`usb_dev.c`,
`diag.cpp`); everything else defaults to 128 (`mk20dx128.c`). Keypad /INT
lines are polled, not interrupts (`input.cpp service()`).

## Buses and devices

| Bus | Pins | Devices | Source |
|---|---|---|---|
| I2C0, 400 kHz | PTD8 SCL / PTD9 SDA (PCR 0x264) | SGTL5000 0x0A, Si4703 0x10, button TCA8418 0x34 | spec 01 §11; `board.h` |
| I2C2, 400 kHz | PTA12 SCL / PTA13 SDA (PCR 0x564) | pad TCA8418 0x34, IS31FL3731 0x74, fuel gauge 0x36 (UNVERIFIED) | spec 01 §7.2–§7.6 |
| SPI2, 4.5 → 30 MHz | PTD12 SCK, PTD13 SOUT, PTD14 SIN, CS PTD15, PD PTD11 | FT81x | spec 01 §4.1 |
| I2S0 master | MCLK PTE6, BCLK PTE12, FS PTE11, TXD PTE10, RXD PTE7 | SGTL5000 | spec 01 §3.2, §5.2 |
| SDHC 4-bit | PTE0–5, CD PTD0 | SD card | spec 01 §6 |
| UART4 31,250 Bd | TX PTE24 (inverted), RX PTE25 | MIDI DIN | spec 01 §8 |
| FlexBus | 39 pins ALT5 | SDRAM | spec 01 §2 |

I2C is polled (`i2c_bus.*`). A wait expires only after 1 ms **and** 2,000
polls, because the audio ISR can preempt the loop longer than 1 ms
(`board.h` `kI2cMinPolls`); a stuck bus is recovered by 16 SCL toggles, and
the clock gate must be on before any I2C register access (`i2c_bus.cpp`,
APPROACH §1). Other GPIOs: power switch PTC3 (LOW = pressed), rail enable
PTA24, update request PTA1, status LED PTC19, mute PTD6, radio switch PTE26,
Si4703 reset PTE27 (spec 01 §3.2, §10).

## Unknowns

- PTA24, PTC19, PTE26, PTA1 physical function; mute polarity of PTD6 (spec 01 §12.6; `board.h` UNVERIFIED).
- SDRAM chip and mode word at 0x70001800 (§12.2).
- DMA channel numbers at runtime (§12.7); confirm from the boot log.
- Whether SDHC DMA can target SDRAM (`kSdDmaToSdram = false`).
- Whether SRAM_L is cacheable for DMA (`sdhc.cpp cacheClearAll()` comment, UNVERIFIED).
- 16 MHz crystal as the BLPE clock in soft-off (`board.cpp` UNVERIFIED).
- Fuel gauge identity / presence (§12.5).
- Stack high-water mark: not measured.
