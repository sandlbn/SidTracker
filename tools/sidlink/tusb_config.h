// SIDLink -- TinyUSB configuration: host on the native USB port, a hub, two
// USB-MIDI devices (the SIDTracker and the USBSID-Pico).
#ifndef SIDLINK_TUSB_CONFIG_H
#define SIDLINK_TUSB_CONFIG_H

#define CFG_TUH_ENABLED 1
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_HOST
#define BOARD_TUH_RHPORT 0
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX 4      // hub + Tracker + USBSID-Pico + one spare
#define CFG_TUH_MIDI 2
#define CFG_TUH_MIDI_RX_BUFSIZE 512
#define CFG_TUH_MIDI_TX_BUFSIZE 512

#endif
