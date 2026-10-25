#!/usr/bin/env bash
# Computer as the SIDLink bridge (Linux, ALSA): connects SIDTracker's
# USB-MIDI port to the USBSID-Pico. Run it after plugging either in, then
# toggle Config > ASID OUT off and on so the USBSID gets the ASID start.
set -euo pipefail
client() { aconnect -l | sed -n "s/^client \([0-9]*\): '$1.*/\1/p" | head -n1; }
t="$(client SIDTracker)"; u="$(client USBSID-Pico)"
[ -n "$t" ] || { echo "SIDTracker not found (USB plugged in, Tracker running?)"; exit 1; }
[ -n "$u" ] || { echo "USBSID-Pico not found"; exit 1; }
aconnect "$t:0" "$u:0" 2>/dev/null || true
echo "SIDTracker ($t:0) -> USBSID-Pico ($u:0) connected; now toggle Config > ASID OUT off / on"
