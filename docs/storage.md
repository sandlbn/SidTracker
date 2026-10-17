# Storage

Plan §3: SD-card interface, filesystem support, throughput, non-volatile
settings, and the native project file plan (plan §15). Sources: OpenTracker
(`ReversedPolytracker-1.9x`, branch `opentracker`, HEAD `0849cb0`). Drivers
`reimpl/src/hal/sdhc.*`, `sd_storage.*`, `nvstore_eeprom.*`, reused as
`src/platform/storage/`; FatFs as `third_party/fatfs`.

## SD card interface (SDHC)

| Item | Value | Source |
|---|---|---|
| Controller | K66 SDHC (0x400B1000), 4-bit bus, 512-byte blocks | spec 01 §6; `sdhc.h` |
| Pins | PTE0–5 (D1, D0, CLK, CMD, D3, D2); PCR 0x103 before init, 0x443 data/CMD, 0x440 CLK | spec 01 §3.2, §6; `board.h` |
| Card detect | PTD0 (Teensy 2), pull-up, LOW = inserted, 500 ms debounce; insert → init + mount, removal → unmount | spec 01 §6; `sd_storage.cpp service()` |
| Clocks (from 180 MHz) | 375 kHz identification; 45 MHz high speed after CMD6 (stock asks 50 MHz); 22.5 MHz default speed; 11.25 MHz last resort | `board.h kSd*Clkfs/Dvs` |
| Transfer | ADMA2 multi-block, 8 descriptors × 32 KB per command; polled PIO through DATPORT as runtime fallback | `board.h kSdAdma*`; `sdhc.h` |
| Bounce | 8 KB SRAM buffer for targets that are unaligned, cross SRAM_L/SRAM_U, or lie in SDRAM (`kSdDmaToSdram = false`) | `board.h`; `sdhc.cpp` |
| Cache | whole LMEM cache cleared around DMA touching SRAM_L (UNVERIFIED that it is needed) | `sdhc.cpp cacheClearAll()` |
| Waits | **polled**, no SDHC interrupt (`IRQSIGEN = 0`); data timeout 500 ms + 1 ms per block | `sdhc.cpp` |
| Errors | retry once, then step the clock down / ADMA2 → PIO (`degrade()`); counters in `sdhc::info()` | `sdhc.cpp`; `sdhc.h Info` |
| Self-check | at init sector 0 is read by PIO at 22.5 MHz and compared with the high-speed and ADMA2 reads | `docs/HARDWARE_TESTING.md` "Known unverified" |

All SD access runs in the main loop and blocks it; the audio ISR keeps running
(it preempts the polled waits). Reading samples or projects during playback
is therefore safe for audio but stalls UI and input for the duration.

## Filesystem: FatFs R0.15a

Configuration from `third_party/fatfs/ffconf.h`:

| Option | Value | Meaning |
|---|---|---|
| `FF_FS_READONLY` / `FF_FS_MINIMIZE` | 0 / 0 | full read/write API |
| `FF_USE_LFN`, `FF_MAX_LFN` | 1, 255 | long names, static working buffer |
| `FF_CODE_PAGE` | 437 | US OEM code page for 8.3 names |
| `FF_FS_EXFAT` | **0** | no exFAT: SDXC cards must be formatted FAT32 |
| `FF_LBA64` | 0 | 32-bit LBA (≤ 2 TB) |
| `FF_VOLUMES` | 1 | one volume, root `""` |
| `FF_FS_NORTC` | 1 | fixed timestamp 2024-11-01 on every file |
| `FF_FS_REENTRANT` / `FF_FS_LOCK` | 0 / 0 | not thread safe, no lock: never call from an ISR |
| `FF_USE_MKFS`, `FF_USE_FASTSEEK` | 0, 0 | no formatting, no fast seek |

`SdStorage` (`sd_storage.*`) implements `IStorage` (`hal_api.h`): `open`
(read / write-truncate / read-write), `read`, `write`, `seek`, `size`,
`close`, `exists`, `mkdir` (one level), `remove`, `rename`, `listDir` with a
callback. Paths use `/`, relative to the card root. Up to **14** files open at
once (`kMaxFiles`, 14 `FIL` = 7,728 B SRAM). `CTRL_SYNC` is a no-op because
writes complete synchronously (`disk_ioctl`). `setStorageMode(true)` releases
the card (USB storage mode has no hardware path, spec 01 §6.1).

The simulator maps a host folder as the card (`sim/src/host_storage.*`):
case-insensitive lookups like FAT, max 32 open files, no timing or errors
(`sim/README.md`).

## Throughput

No figure is recorded in the sources. The selftest measures it once after
start with audio running: 1 MB temp file `OT_BENCH.TMP` in 64 KB chunks from
SDRAM, written, read back, verified, deleted, logged as `sd bench: OK,
1048576 bytes, write W.WW MB/s, read R.RR MB/s, verify errors 0 (ADMA2
45000000 Hz)` (`docs/HARDWARE_TESTING.md` §2; `SdStorage::benchmark`). SDRAM
sources bounce through the 8 KB SRAM buffer. Record the line at M0.

## Non-volatile settings (FlexRAM EEPROM)

| Item | Value | Source |
|---|---|---|
| Window | FlexRAM emulated EEPROM, 4 KiB at 0x14000000 | spec 02 §9; `nvstore_eeprom.cpp` |
| Byte 0 | update / power flag (only `requestFirmwareUpdate` writes it) | spec 01 §10; `fw_update.cpp` |
| App area | physical byte 10 onwards; `INvStore` offset 0 = byte 10, size 4,086 | spec 02 §9; `nvstore_eeprom.cpp kAppBase` |
| Stock content | config image (0xF4 bytes) starting with the ASCII magic `EEPROM`; if the magic is missing stock reads the update flag as 1 | spec 01 §10; spec 02 §9; `reimpl/README.md` |
| Guard | read/write only when `eepromPartitionUsable()` (EEERDY set, RAMRDY clear) so the Teensy core never repartitions the unit | `fw_update.h`; `nvstore_eeprom.cpp` |
| Write | byte by byte, only changed bytes, IRQs off per byte, then restore `SIM_CLKDIV1` OUTDIV3 = 1 | `nvstore_eeprom.cpp write()` |

Audio side effect (derived, not observed): the Teensy `eeprom_write_byte()`
leaves HSRUN, which for 180/90 MHz sets `SIM_CLKDIV1 OUTDIVS(1,1,1,7)`, i.e.
the core clock at 90 MHz for the duration (`teensy3/mk20dx128.c
kinetis_hsrun_disable`), with interrupts disabled by `nvstore_eeprom.cpp`.
I2S MCLK is taken from the system clock (MCR 0x40000000, spec 01 §5.2), so
each EEPROM byte write while audio runs can shift MCLK and delay the audio
ISR. SIDTracker should save settings to EEPROM only while muted or stopped,
or keep its own settings on the SD card.

## Native project files (plan §15)

Not covered by the sources; the OpenTracker formats (spec 02) are Polyend
project files and are not reused. What the platform offers for SIDTracker's
own format: `IStorage` file API with long names, 14 open files, 16 MB SDRAM
for in-memory patterns and undo (APPROACH §2), and write-then-rename for
atomic saves (`rename` is available; FatFs `f_rename` fails if the target
exists, so remove first). Directory layout, file format and versioning are
decided in plan §15 work, not here.

## Unknowns

- SD throughput on hardware (pending the selftest bench line).
- Whether SDHC DMA can write to SDRAM directly (`kSdDmaToSdram`, UNVERIFIED); until then every SDRAM transfer bounces.
- Whether SRAM_L needs the cache clear for DMA (`sdhc.cpp` UNVERIFIED).
- Whether cards above 32 GB formatted exFAT are common for users (FatFs built without exFAT).
- Duration of one FlexRAM EEE byte write, and whether it disturbs audio (derived above, not measured).
- Where stock keeps settings beyond the EEPROM image, and the full effect of a missing `EEPROM` magic (spec 02 §9, §12).
