// SIDTracker -- MIDI building blocks: an input parser and the ASID encoder.
//
// ASID (SID register frames over MIDI SysEx, read by SID Factory II style
// players and real-SID interfaces such as USBSID-Pico):
//   F0 2D 4C F7                     start
//   F0 2D 4D F7                     stop
//   F0 2D cmd m0 m1 m2 m3 h0 h1 h2 h3 d... F7
//     cmd 4E = SID 1, 50 / 51 / 52 = SIDs 2-4; m = 28-bit mask of the
//     registers in this frame, h = their bit 7, in 7-bit groups (m0 = bits
//     0-6); d = the low 7 bits of each register in bit order. Bit order:
//     registers 0-3, 5-10, 12-17, 19-24, then the control registers 4, 11,
//     18 (written last, after frequency and envelope), then bits 25-27: a
//     second write to control register 4 / 11 / 18 in the same frame (a
//     gate retrigger), applied after the first.
// Protocol from public descriptions (rev 2, 2003) and the register order
// used by current receivers; no third-party code.
#pragma once

#include <cstdint>

namespace sidt {
namespace midi {

// Running status, real-time bytes (F8-FF) anywhere, SysEx skipped.
class Parser {
public:
    struct Msg {
        uint8_t status = 0, d1 = 0, d2 = 0;   // real-time: status only
    };
    // Feeds one byte; true when a message is complete (in `m`).
    bool feed(uint8_t b, Msg& m);

private:
    uint8_t status_ = 0, data_[2] = {}, have_ = 0;
    bool sysex_ = false;
};

class AsidEncoder {
public:
    static constexpr int kChips = 2;
    static constexpr uint32_t kMaxMessage = 3 + 8 + 28 + 1;   // one chip's frame
    // One SID register write of this frame (audio context).
    void write(uint8_t chip, uint8_t reg, uint8_t val);
    // Appends this frame's messages (one per chip that changed) to `out`
    // and starts a new frame. Returns the bytes written; `cap` must hold
    // kChips * kMaxMessage.
    uint32_t flush(uint8_t* out, uint32_t cap);
    bool pending() const { return mask_[0] || mask_[1]; }
    void clear() { mask_[0] = mask_[1] = image_[0] = image_[1] = 0; }
    // One chip's message (0 bytes when nothing changed); `taken` gets the
    // registers it carried, so a refused send can put them back (restore):
    // they then go out with the next frame.
    uint32_t build(int chip, uint8_t* out, uint32_t& taken);
    void restore(int chip, uint32_t taken) {
        mask_[chip] |= taken;
        image_[chip] |= taken & sent_[chip];   // image values stay replaceable
    }
    // The register image (after a reset or when a receiver connects): every
    // register at its current value, no retrigger slot.
    void set(uint8_t chip, uint8_t reg, uint8_t val);
    static uint32_t start(uint8_t* out);   // F0 2D 4C F7
    static uint32_t stop(uint8_t* out);    // F0 2D 4D F7

private:
    uint8_t val_[kChips][28] = {};   // by ASID bit position
    uint32_t mask_[kChips] = {};
    uint32_t image_[kChips] = {};    // slots holding a set() value: a write replaces it
    uint32_t sent_[kChips] = {};     // image_ of the last build(), for restore()
};

}  // namespace midi
}  // namespace sidt
