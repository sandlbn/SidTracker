// SIDTracker -- SID register map and bit names (MOS 6581/8580 datasheet).
#pragma once

#include <cstdint>

namespace sidt {
namespace sid {

// Register numbers (MOS 6581/8580 datasheet register map).
enum Reg : uint8_t {
    kFreqLo = 0x00, kFreqHi = 0x01, kPwLo = 0x02, kPwHi = 0x03, kControl = 0x04, kAD = 0x05, kSR = 0x06,
    kVoiceStride = 7,
    kFcLo = 0x15, kFcHi = 0x16, kResFilt = 0x17, kModeVol = 0x18,
    kPotX = 0x19, kPotY = 0x1A, kOsc3 = 0x1B, kEnv3 = 0x1C,
    kRegCount = 0x20,
};
// Control register bits.
enum Ctrl : uint8_t {
    kGate = 0x01, kSync = 0x02, kRing = 0x04, kTest = 0x08,
    kTri = 0x10, kSaw = 0x20, kPulse = 0x40, kNoise = 0x80,
};
// MODE/VOL register bits.
enum ModeVol : uint8_t { kLP = 0x10, kBP = 0x20, kHP = 0x40, k3Off = 0x80 };

}  // namespace sid
}  // namespace sidt
