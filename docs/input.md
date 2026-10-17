# Input

Plan §3: pad matrix scanning, buttons, jog wheel / encoder, power switch, pad
LEDs, event timing. Sources: OpenTracker (`ReversedPolytracker-1.9x`, branch
`opentracker`, HEAD `0849cb0`). Drivers `reimpl/src/hal/input.*` and
`pad_leds.*`, reused as `src/platform/input/`.

## Interface

`IInput::poll(InputEvent&)` pops a queued event: `kButtonDown/Up`,
`kPadDown/Up`, `kEncoder`, with `id`, `velocity` (pads: always 127 on press,
there is no pressure sensing), `delta` (encoder, accelerated) and `timeMs`
(`src/core/hal_api.h`). The queue holds 64 events and drops the newest when
full (`input.h kQueue`, `input.cpp push()`). `poll()` also runs `service()`.
Button ids are the canonical ones in `src/core/ui/buttons.h` (F1–F8 = 0–7,
module keys 0x08–0x11, editing keys 0x12–0x22, Power = 0x2a virtual).

All input runs in the main loop: the keypad /INT lines are **polled** in
`service()` (no pin interrupts), I2C is polled, the encoder is read every
≥ 101 ms. Latency is therefore bounded by the main-loop period (logged as
`loop max` in telemetry, `app_main.cpp`).

## Buttons: TCA8418 on I2C0

| Item | Value | Source |
|---|---|---|
| Chip / address | TCA8418 keypad scanner, I2C0, 0x34 | spec 01 §7.1; `board.h` |
| Matrix | 8 rows × 9 columns (`KP_GPIO1 0xFF, GPIO2 0xFF, GPIO3 0x01`); 33 buttons, ids 0–32 | spec 01 §7.1 |
| Config | `CFG 0xB9` (auto-increment, overflow, key-event IRQ), `INT_STAT 0x0F` clear | spec 01 §7.1 |
| /INT | PTD1 (Teensy 14), pull-up, polled LOW | `input.cpp service()` |
| Read | `KEY_LCK_EC` low nibble = count; each `KEY_EVENT_A`: bit 7 press, code = row·10 + col + 1 | spec 01 §7.1 |
| Map | rows 0–4 × cols 0–4 → ids 8–32; row 7 cols 0–7 → ids 7…0 | `input.cpp buttonFromCode()`; spec 05 §2.1.1 |
| Debounce | chip default, no software debounce | spec 01 §7.1 [I] |
| Auto-repeat | ids 0x19–0x1B, 0x1E–0x21 re-sent as presses after 333 ms, then 233, 143, 98, 78, 63 ms | `input.cpp serviceHolds()`; spec 01 §7.1 |
| Watchdog | every 1 s CFG read back; mismatch → bus recovery, release all held keys, re-init | `input.cpp watchdog()` |

There is no "hold" event type in `IInput`; holds of non-repeating keys must
be timed by the application from Down/Up `timeMs`.

## Pads: TCA8418 on I2C2

| Item | Value | Source |
|---|---|---|
| Chip / address | second TCA8418, I2C2 (the "pad bus", `board::kPadBusIndex = 2`), 0x34 | spec 01 §7.2; `board.h` |
| Matrix | 8 rows × 10 columns (`GPIO3 0x03`); 48 pads, 12 × 4, id = row·12 + col | spec 01 §7.2 |
| /INT | PTC11 (Teensy 38), polled | `input.cpp` |
| Map | codes in rows 0–3 → columns 0–9; pad columns 10/11 are keypad rows 4–7, cols 9/8 | `input.cpp padFromCode()` (checked on the device 2026-10-15; spec 01 and spec 05 disagree) |
| Velocity | none (on/off only, no ADC) | spec 01 §7.2 |

## Encoder (jog wheel): FTM2 quadrature decoder

Exact stock write sequence (spec 09 §2) in `PanelInput::init` /
`input.cpp`: PTA10/PTA11 PCR 0x612, FTM2 `MODE 0x05`, `FILTER 0x44`,
`QDCTRL 0xC1`, `C0V` 0x6000/0xA000 by QUADIR, `C0SC 0x50`. The counter
runs without a clock source (SC = 0) and no FTM2 interrupt is installed
(spec 09 §3).

Poll (`serviceEncoder`, spec 09 §4):

1. at most once per > 100 ms;
2. read CNT, write 0; values 0x4000–0xC000 are an overflow band and are discarded;
3. 4 counts per detent; the remainder carries over only while the direction is unchanged;
4. acceleration table (mode 2): steps 0–9 → 0, 1, 2, 6, 12, 20, 30, 45, 60, 80; ≥ 10 → 100;
5. one `kEncoder` event with the accelerated `delta` (int8).

Consequence for SIDTracker: one event per 100 ms window and nonlinear
deltas. Fine for menus, wrong for a value editor or Perform. APPROACH §1 plans
a local raw-detent flag (unaccelerated detent count). The encoder push is a
keypad button (`btn::EncPush` 0x21).

SIDTracker never relies on the jog push alone (owner, 2026-10-16: no
usable confirm by pushing the wheel): every action that takes it also
takes Insert or an F-key shown on the bottom bar (Config F8 INSTALL, Song
F7 EDIT PAT, SOUNDS / tables / Perform / kit preview: Insert).

## Power switch

PTC3 (Teensy 9), LOW = pressed, no pull (`board.h`). Handled in
`board.cpp service()`: 20 ms debounce, delivered as button 0x2a down/up; a
press already held at boot is ignored until released; held 1,500 ms without
Shift held at the press → `powerOff()` (hook, mute, "Powering off...",
`softOff()`: peripherals and rail off, VLPS, wake on PTC3, reset). Shift +
Power is the screenshot chord in OpenTracker (spec 05; `HARDWARE_TESTING.md`).

## Pad LEDs: IS31FL3731 on I2C2

| Item | Value | Source |
|---|---|---|
| Chip / address | IS31FL3731, I2C2, 0x74; single-colour LEDs | spec 01 §7.3; `pad_leds.h` |
| `ILeds::setPad(r,g,b)` | brightness = max(r,g,b) >> 3 → level 0–31 → gamma table → PWM | `pad_leds.cpp` |
| Position | idx + (idx/12)·4 → PWM reg 0x24 + row·16 + col | spec 01 §7.3 |
| Flush | one 181-byte burst (18 enable, 18 blink, 144 PWM), only when dirty and ≥ 30 ms after the last | `pad_leds.cpp show()/service()`; `board.h kLedFlushMinMs` |
| Watchdog | every 1 s: shutdown register must read 1, else recover + re-init, keeping the levels | `pad_leds.cpp service()` |

A flush is about 181 × 9 bits at 400 kHz ≈ 4 ms of polled I2C in the main loop
(estimate from the bus rate, not measured), at most every 30 ms. The pad bus
alternates pad reads and LED flushes (spec 01 §7.5).

## Simulator

`sim/src/host_hal.*` and `panel.*`: panel with F1–F8, 12 × 4 pads, encoder and
the 5 × 5 key block; keyboard and mouse map; scripts with `press`, `pad N
down|up|tap`, `enc DELTA`; virtual time in headless runs. Encoder acceleration
and I2C scanning are not simulated (`sim/README.md`).

## Unknowns

- Physical encoder direction (CW ↔ positive count) and 4 counts = one mechanical detent (spec 09 "Open unknowns"; `HARDWARE_TESTING.md`).
- Front-panel function of some button ids beyond the spec 05 map (spec 01 §12.4).
- Pad brightness Low/Medium/High mapping (spec 01 §7.3, §12.4).
- Button bounce behaviour without software debounce (spec 01 §7.1 [I]).
- Main-loop latency of input events on hardware: pending M0 telemetry.
