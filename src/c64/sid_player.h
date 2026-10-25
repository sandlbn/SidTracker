// SIDTracker -- SID player: plays C64 .sid files (PSID) on the dual-SID
// engine. MIT, SIDTracker.
//
// The tune's own 6502 code runs on src/c64/mos6502.h in a minimal C64: 64 KB
// RAM, the $01 banking of BASIC / KERNAL / I/O, a KERNAL stub (the IRQ entry
// $FF48 -> ($0314) and its exit $EA31 / $EA81, vectors; no Commodore ROM
// code), the SIDs at $D400 and the file's second-SID address, CIA 1 timer A
// latch writes (the play rate) and a VIC raster counter for players that
// poll it. One frame() runs the play routine (or the interrupt the init
// routine installed) once and returns the SID writes it made.
//
// Not covered (yet): RSID files (need a full C64), sample ("digi") playback
// that relies on cycle timing inside a frame, a third SID.
#pragma once

#include <cstdint>

#include "c64/mos6502.h"

namespace sidt {
namespace c64 {

struct SidWrite {
    uint8_t chip, reg, val;
};

struct SidInfo {
    char name[33] = {}, author[33] = {}, released[33] = {};
    uint16_t version = 0, load = 0, init = 0, play = 0, songs = 0, start = 0;
    bool ntsc = false;
    uint8_t model[2] = {2, 2};       // 1 = 6581, 2 = 8580, 0 = unknown
    uint16_t sid2 = 0;               // second SID address, 0 = none
    uint16_t sid3 = 0;               // third SID address (not emulated: its writes are dropped)
    uint32_t speed = 0;              // bit n: song n + 1 uses the CIA timer
};

class SidPlayer final : public Bus {
public:
    static constexpr uint32_t kRamBytes = 65536, kRomBytes = 8192;
    static constexpr int kMaxWrites = 64;

    // C64 RAM (64 KB) and the KERNAL stub (8 KB): SDRAM on the device.
    void setMemory(uint8_t* ram, uint8_t* rom);
    // Parses and loads a PSID file. False (and `err` says why) for RSID,
    // a bad header or data that does not fit.
    bool load(const uint8_t* file, uint32_t size, SidInfo& info, const char*& err);
    // Runs the init routine for song `song` (1-based), right after load()
    // (another song: load the file again). Its SID writes (volume, filter
    // set-up: many tunes write them only here) go to `out` like a frame's.
    // False if it crashes.
    bool start(int song, SidWrite* out = nullptr, int* n = nullptr);
    // One frame: the play routine (or interrupt) once; its SID writes in
    // `out` (in order, at most kMaxWrites: later writes to a register then
    // replace earlier ones). False when the tune crashed or hung.
    bool frame(SidWrite* out, int& n);
    // Play calls per second of the started song, in milli-Hz.
    uint32_t rateMilliHz() const { return rate_; }
    uint32_t clockHz() const { return info_.ntsc ? 1022727u : 985248u; }
    const SidInfo& info() const { return info_; }
    int song() const { return song_; }
    // Why the last start() / frame() failed: an undocumented opcode (its
    // value) or 0 = the routine did not return in time.
    uint8_t failOpcode() const { return cpu_.bad ? cpu_.badOpcode : 0; }

    // Bus ($A000-$FFFF reads, $D000-$DFFF writes).
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t v) override;

private:
    void buildRom();
    void setRamVectors(uint16_t load, uint32_t len);
    uint8_t bankFor(uint16_t addr) const;
    void record(uint8_t chip, uint8_t reg, uint8_t v);

    Mos6502 cpu_;
    uint8_t* ram_ = nullptr;
    uint8_t* rom_ = nullptr;
    SidInfo info_;
    int song_ = 0;
    uint32_t rate_ = 50125;
    uint16_t ciaLatch_ = 0;
    bool ciaWritten_ = false;
    uint16_t raster_ = 0;
    uint32_t rnd_ = 0x13579BDF;
    SidWrite* out_ = nullptr;
    int n_ = 0;
};

}  // namespace c64
}  // namespace sidt
