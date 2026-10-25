# SIDLink: Pico 2 bridge from SIDTracker to real SID chips

A Raspberry Pi Pico 2 (any RP2350 board) as USB host between the Polyend
Tracker running SIDTracker and a USBSID-Pico with real 6581 / 8580 chips.
The Tracker can only be a USB device, so it cannot drive the USBSID-Pico
itself; the Pico 2 hosts both behind a powered hub and forwards the
Tracker's MIDI (ASID frames, notes, clock) to the USBSID-Pico.

```
 Tracker (SIDTracker, Config > ASID OUT = USB) ──┐
                                                 ├── powered USB hub ── OTG adapter ── Pico 2 (SIDLink)
 USBSID-Pico (cafe:4011) ────────────────────────┘          │
                                           hub supply 5 V ──┴── Pico VBUS (pin 40) + GND
```

- The Pico's own USB port is the host port: an OTG adapter (USB-C or
  micro-USB to USB-A female) into the hub's upstream cable.
- Power: the hub's own supply (5 V, 2.5 A or more). The Pico takes 5 V on
  VBUS (pin 40) and GND (pin 38) from a spare hub port or the supply; that
  also powers the hub's upstream connection.
- LED: three quick blinks at power-up (powered, SIDLink running). Then on
  while the Tracker (any USB-MIDI source) and the USBSID-Pico are both
  connected, flickering while data flows; otherwise blinks repeating every
  1.5 s: 1 = nothing on USB (hub link, OTG adapter), 2 = USB devices but no
  MIDI device, 3 = only the Tracker, 4 = only the USBSID-Pico.

## Build and install

```
git clone https://github.com/raspberrypi/pico-sdk ~/pico/pico-sdk   # SDK 2.3.1
git -C ~/pico/pico-sdk submodule update --init lib/tinyusb
git -C ~/pico/pico-sdk/lib/tinyusb checkout 0.21.0                  # MIDI host driver
mkdir build && cd build
PICO_SDK_PATH=~/pico/pico-sdk cmake -G Ninja .. && ninja           # -> sidlink.uf2
```

Hold BOOTSEL, plug the Pico into a computer, copy `sidlink.uf2` onto the
`RP2350` drive. The board then runs SIDLink (it no longer shows up on a
computer: its USB port is a host now). To change it again, hold BOOTSEL
while plugging it in.

Status (2026-10-19): builds; flashed onto the owner's RP2350 board. The
USBSID-Pico (USBSID-Pico2 v1.3, firmware LouD v0.7.6) enumerates as cafe:4011
and plays ASID as shipped (ASID on by default; it needs a steady frame
stream, which SIDTracker sends). SIDTracker -> computer -> USBSID-Pico
(`aconnect`) plays on the real chips. Not yet working through the hub: the
Pico LED stayed off on the first try.

### Without the Pico: a computer as the bridge (Linux)

Both devices on the computer's USB; `aconnect -l` shows them, then
`tools/sidlink/pc_bridge.sh` (or `aconnect <tracker client>:0 <usbsid client>:0`). Toggle
Config > ASID OUT off and on afterwards so the USBSID gets the ASID start.
