// sidtrace -- host-side oracle: runs a PSID file's init and play routines on
// a small 6502 emulator and prints the SID register state after every play
// call. Used to compare SIDTracker's player with songs packed by
// GoatTracker's own gt2reloc (docs/goattracker-format.md "Oracle"): the
// .sid is input data run out of process; no GoatTracker code is read or
// linked here (DECISIONS.md 2, 3).
//
//   sidtrace FILE.sid [--song N] [--frames N]
// Output: one line per frame, "frame: r00 r01 ... r18" (hex, 25 registers
// of the SID at $D400), plus "# writes" comment lines with the per-frame
// write order when --writes is given.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cpu6502.h"

using oracle::Cpu;


int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: sidtrace FILE.sid [--song N] [--frames N] [--writes]\n");
        return 2;
    }
    int song = -1, frames = 3000;
    bool writes = false;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--song") && i + 1 < argc) song = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--writes")) writes = true;
    }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::perror(argv[1]);
        return 1;
    }
    std::vector<uint8_t> b;
    for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(uint8_t(c));
    std::fclose(f);
    if (b.size() < 0x7C || (std::memcmp(b.data(), "PSID", 4) && std::memcmp(b.data(), "RSID", 4))) {
        std::fprintf(stderr, "sidtrace: not a PSID/RSID file\n");
        return 1;
    }
    auto be16 = [&](size_t o) { return uint16_t((b[o] << 8) | b[o + 1]); };
    const uint16_t dataOff = be16(6);
    uint16_t load = be16(8);
    const uint16_t init = be16(10), play = be16(12), songs = be16(14), start = be16(16);
    size_t pos = dataOff;
    if (!load) {
        load = uint16_t(b[pos] | (b[pos + 1] << 8));
        pos += 2;
    }
    Cpu cpu;
    for (size_t i = pos; i < b.size() && load + (i - pos) < 0x10000; ++i) cpu.mem[load + (i - pos)] = b[i];
    if (song < 0) song = start ? start - 1 : 0;
    std::printf("# sidtrace load $%04X init $%04X play $%04X songs %u song %d\n", unsigned(load), unsigned(init),
                unsigned(play), unsigned(songs), song);
    if (!cpu.call(init, uint8_t(song))) return 1;
    for (int fr = 0; fr < frames; ++fr) {
        cpu.writes.clear();
        if (!cpu.call(play, 0)) return 1;
        if (writes) {
            std::printf("# writes %d:", fr);
            for (auto& w : cpu.writes) std::printf(" %02X=%02X", w.first, w.second);
            std::printf("\n");
        }
        std::printf("%d:", fr);
        for (int r = 0; r < 0x19; ++r) std::printf(" %02X", cpu.sid[r]);
        std::printf("\n");
    }
    return 0;
}
