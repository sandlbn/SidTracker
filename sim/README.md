# SIDTracker desktop simulator

The portable app (`src/app`, `src/ui`, `src/sid`, `src/tracker`,
`src/audio`) runs unchanged against host implementations of the HAL
interfaces. The host HALs in `sim/src` (all files except `main.cpp`) are
vendored from OpenTracker (`src/platform/PROVENANCE.md`); `sim/src/main.cpp`
is SIDTracker's.

```
make -C sim                       # build/sim/sidtracker-sim
build/sim/sidtracker-sim          # window; SD card = ~/.sidtracker-sd
make -C sim check                 # headless smoke run -> build/sim/smoke-*.png, smoke.wav
```

Options: `--sd DIR`, `--scale 2`, `--no-audio`, `--quit-after MS`; headless:
`--script FILE`, `--screenshot OUT.png`, `--panel`, `--wav OUT.wav`.

Differences from the device: audio runs at 44,100 Hz (device 44,117.6 Hz;
pitch is exact on both because the SID clock step uses the real rate); the
benchmark page shows host time scaled to 180 MHz, not K66 cycles; there is
no underrun counter and no firmware update; the FT81x display-list size
limit is not modelled.

## Keys

Tab or Caps Lock switches between COMMAND and PAD mode.

| Key | Control |
|---|---|
| F1–F8 | screen buttons |
| F9 / F10 / F12 | Pattern / Song / Help |
| arrows, Shift, Space (Play), Insert, Delete, Enter (jog push) | as labelled |
| Page Up / Page Down, mouse wheel, `[` `]` | jog wheel |
| COMMAND mode: R Rec, C Copy, N / I / X / V Note / Instr / Fx1 / Fx2 | |
| COMMAND mode: 1 Instrument, 2 Perform, 3 File, 4 Settings, 5 Benchmark, 0 Song | module keys |
| PAD mode: `1`…`=`, `Q`…`]`, `A`…`\`, `Z`…`/` | pad rows 0–3 (bottom row = octave) |
| Ctrl+Q | quit |

The mouse works on every panel control; right-click latches a button.

## Scripts

`wait MS`, `press` / `release` / `tap NAME [MS]` / `hold NAME [MS]`,
`pad N down|up|tap [VEL]`, `enc DELTA`, `screenshot FILE`, `panelshot FILE`,
`echo TEXT`. Button names are the panel names (`PLAY`, `REC`, `F1`,
`INSTRPARAMS`, `PERFORMANCE`, `SONG`, `CONFIG`, `FILE`, `MASTER`, `PATTERN`,
…) or numeric ids. Time is virtual: one app loop per simulated millisecond,
one audio block every 128 / 44,100 s, so runs are deterministic.
Example: `scripts/smoke.txt`.
