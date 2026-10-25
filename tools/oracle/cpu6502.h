// cpu6502 -- the oracle tools' C64 (sidtrace, sidcompare, psid_test): the
// firmware's 6502 core (src/c64/mos6502.h) on a flat 64 KB RAM with SIDs at
// $D400 / $D420 (reads of OSC3 / ENV3 / pots return 0) and nothing else.
// MIT, SIDTracker.
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "c64/mos6502.h"

namespace oracle {

struct Cpu : sidt::c64::Bus {
    uint8_t mem[65536] = {};
    uint8_t sid[64] = {};   // SID 1 at $D400, SID 2 at $D420
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    sidt::c64::Mos6502 core;
    uint64_t& cycles = core.cycles;
    bool& bad = core.bad;

    Cpu() {
        core.ram = mem;
        core.bus = this;
    }
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;

    uint8_t read(uint16_t ad) override {
        if (ad >= 0xD400 && ad < 0xD440) {
            const uint8_t r = uint8_t(ad - 0xD400);
            return (r & 31) >= 0x19 ? 0 : sid[r];
        }
        return mem[ad];
    }
    void write(uint16_t ad, uint8_t v) override {
        if (ad >= 0xD400 && ad < 0xD440) {
            sid[ad - 0xD400] = v;
            writes.emplace_back(uint8_t(ad - 0xD400), v);
            return;
        }
        mem[ad] = v;
    }
    bool call(uint16_t addr, uint8_t acc) { return core.call(addr, acc); }
};

}  // namespace oracle
