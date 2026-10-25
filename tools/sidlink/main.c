// SIDLink -- Pico 2 USB-host bridge between SIDTracker and a USBSID-Pico.
//
// The Pico's USB port runs as host behind a powered hub. Every USB-MIDI
// device on the hub is a source except the USBSID-Pico (VID:PID cafe:4011),
// the sink: whatever a source sends (ASID frames, notes, clock) goes to the
// sink unchanged. MIT, SIDTracker.
//
// LED: three quick blinks at power-up, then on while a source and the sink
// are both connected (flickering with traffic); otherwise a group of blinks
// every 1.5 s says what is missing:
//   1 nothing on USB   2 USB devices but no MIDI   3 only a source   4 only the sink
#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "tusb.h"

#define USBSID_VID 0xCAFE
#define USBSID_PID 0x4011

static int8_t sink = -1;                    // MIDI index of the USBSID-Pico
static bool source[CFG_TUH_MIDI];           // MIDI indexes that forward to it
static uint32_t traffic_until;
static int devices;                         // USB devices mounted (any class)

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

void tuh_mount_cb(uint8_t daddr) {
    (void)daddr;
    ++devices;
}

void tuh_umount_cb(uint8_t daddr) {
    (void)daddr;
    if (devices) --devices;
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t* info) {
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(info->daddr, &vid, &pid);
    if (vid == USBSID_VID && pid == USBSID_PID) {
        sink = (int8_t)idx;
        source[idx] = false;
    } else {
        source[idx] = true;
    }
}

void tuh_midi_umount_cb(uint8_t idx) {
    if (sink == idx) sink = -1;
    source[idx] = false;
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred) {
    (void)xferred;
    uint8_t packet[64];
    // Forward whole 4-byte USB-MIDI event packets: cable and code index
    // stay as the source framed them, so SysEx (ASID) passes intact.
    for (;;) {
        const uint32_t n = tuh_midi_packet_read_n(idx, packet, sizeof packet);
        if (!n) break;
        if (source[idx] && sink >= 0) {
            tuh_midi_packet_write_n((uint8_t)sink, packet, n);
            traffic_until = now_ms() + 30;
        }
    }
    if (sink >= 0) tuh_midi_write_flush((uint8_t)sink);
}

// Blink code: `count` 120 ms blinks at the start of each 1.5 s cycle.
static bool blinks(int count, uint32_t t) {
    const uint32_t ph = t % 1500u;
    return ph < (uint32_t)count * 250u && (ph % 250u) < 120u;
}

int main(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    for (int i = 0; i < 3; ++i) {   // powered and running
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(80);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(120);
    }
    tusb_rhport_init_t host_init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
    tusb_init(BOARD_TUH_RHPORT, &host_init);
    for (;;) {
        tuh_task();
        bool any = false;
        for (int i = 0; i < CFG_TUH_MIDI; ++i) any |= source[i];
        const uint32_t t = now_ms();
        bool led;
        if (any && sink >= 0) led = !((int32_t)(traffic_until - t) > 0 && ((t / 40) & 1));
        else if (any) led = blinks(3, t);
        else if (sink >= 0) led = blinks(4, t);
        else led = blinks(devices ? 2 : 1, t);
        gpio_put(PICO_DEFAULT_LED_PIN, led);
    }
}
