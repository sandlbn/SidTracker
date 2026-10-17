# Decisions

Answers to the open questions in APPROACH.md (2026-10-16).

1. **Licence: MIT** (owner's decision), see `LICENSE`. OpenTracker is also
   MIT for its own code (`LICENSE` at the OpenTracker repo root), so the
   vendored HAL can be copied with its notice. Third-party code keeps its own
   licence: the Teensy core is MIT plus a "list PJRC devices in device
   pickers" clause, which does not affect us. FatFs and the fonts (OFL/Apache)
   are compatible as well.
2. **No GPL code in the firmware.** reSID, reSIDfp and GoatTracker code are
   never linked into, or ported to, the firmware. reSIDfp is allowed only as a
   separate host-side reference program for comparison tests.
3. **GoatTracker format research:** use only GoatTracker's documentation
   (readme and format description) and files that GoatTracker produces
   (.sng/.ins fixtures saved with GoatTracker, compared byte by byte). Do not
   read the GoatTracker source code. If something cannot be settled from the
   docs, find it out experimentally with GoatTracker itself, and record it as
   an unknown in docs/goattracker-format.md.
4. **USB identity:** keep 1209:0001 (the pid.codes test PID) during
   development, with product string "SIDTracker". Before the first public
   release, request a dedicated PID from pid.codes (this needs the public MIT
   repo). Tracked as a release blocker.
5. **CommanderSID interop:** write a shared, spec-only description of the
   SID register-write event stream (`docs/sid-event-stream.md`, to be written
   after M3). No shared code, because CommanderSID is GPL.

## Implementation choices (2026-10-16, first implementation; open to review)

6. **Vendored from OpenTracker `0849cb0`**, not `c69d753`: same HAL plus the
   open Roboto fonts and the MIT licence. Radio, codec DAP and EEPROM drivers
   stay vendored unchanged because `board.cpp` uses them; SIDTracker just
   does not call them (`src/platform/PROVENANCE.md`).
7. **Settings live on the SD card** (in the song / autosave), never in the
   FlexRAM EEPROM. An EEPROM write leaves HSRUN and halves the core clock
   while audio runs (`docs/audio.md`, unknowns).
8. **Pattern command = one hex digit + an 8-bit parameter**, shown as three
   digits (`C-4 01 A03`), GoatTracker-shaped. The plan's examples show two
   digits, but one nibble + one byte keeps GoatTracker patterns mappable.
   Command set: `docs/commands.md`.
9. **Tick rate = BPM × 2 / 5 Hz**: 125 = 50 Hz PAL frame (default), 150 =
   60 Hz. Other tempos mark the song Extended (not GoatTracker exportable).
10. **SID engine arithmetic**: chip state (accumulators, LFSR, ADSR counters)
    is integer in SID-clock units; waveform levels, filter and mix are
    single-precision float, which costs the same as fixed point on the M4F
    FPU. Pitch accuracy comes from an 8.24 clocks-per-sample step computed
    from the real output rate (44,117.6 Hz on the device), not from a
    compensated note table.
11. **Pattern memory**: 128 patterns × up to 128 rows × 6 tracks in SDRAM
    (393 KB). Instruments 1–63 and four 255-row tables in SRAM.
12. **First CPU numbers** (QEMU, instructions): 24–26 % average, 39 % peak
    of the block budget against the 10 % estimate. Under the 70 % alarm.
    Further optimisation waits for K66 measurements (PLAN.md §6).
13. **GoatTracker playback is SIDTracker's playback** (2026-10-16). The 0.1
    player had its own semantics modelled on GoatTracker's concepts; it now
    follows GoatTracker 2 exactly where it can (docs/commands.md), so
    imported songs sound as in GoatTracker and native songs export cleanly.
    Native 0.1 projects keep loading; songs that relied on the 0.1 details
    (wave delay order, absolute note offset, gate from the note only) may
    play slightly differently.
14. **GoatTracker oracle.** Undocumented GoatTracker behaviour is measured by
    packing songs with GoatTracker's `gt2reloc` (run as a separate program)
    and tracing the packed player with our own 6502 emulator
    (`tools/oracle/`). Only the output is used. GoatTracker's example songs
    are kept in the git-ignored `fixtures/private/`.
15. **Per-channel tempo is not supported**: SIDTracker's six tracks share
    one row clock (PLAN.md §7 pattern model); `F83`..`FFF` act on all tracks
    and the import report says so.
16. **`.sid` export (owner, 2026-10-16): own 6502 play routine** (option B),
    PSID v3 with dual-SID support, as the last milestone (PLAN.md M9). A
    register-stream dump (option A) was considered and not chosen.
