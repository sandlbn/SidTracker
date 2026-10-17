// cpu6502 -- the small NMOS 6502 emulator of the oracle tools (sidtrace,
// sidcompare): documented opcodes, RAM everywhere, SIDs at $D400 / $D420
// (register reads of OSC3 / ENV3 / pots return 0). MIT, SIDTracker.
#pragma once

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace oracle {

struct Cpu {
    uint8_t mem[65536] = {};
    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, sp = 0xFD, p = 0x24;
    uint8_t sid[64] = {};   // SID 1 at $D400, SID 2 at $D420
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    uint64_t cycles = 0;
    bool bad = false;

    enum : uint8_t { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };

    uint8_t rd(uint16_t ad) {
        if (ad >= 0xD400 && ad < 0xD440) {
            const uint8_t r = uint8_t(ad - 0xD400);
            return (r & 31) >= 0x19 ? 0 : sid[r];   // OSC3/ENV3/pots read 0
        }
        return mem[ad];
    }
    void wr(uint16_t ad, uint8_t v) {
        if (ad >= 0xD400 && ad < 0xD440) {
            sid[ad - 0xD400] = v;
            writes.emplace_back(uint8_t(ad - 0xD400), v);
            return;
        }
        mem[ad] = v;
    }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() {
        const uint16_t lo = fetch();
        return uint16_t(lo | (fetch() << 8));
    }
    void push(uint8_t v) { mem[0x100 + sp--] = v; }
    uint8_t pull() { return mem[0x100 + ++sp]; }
    void nz(uint8_t v) {
        p = uint8_t((p & ~(N | Z)) | (v & N) | (v ? 0 : Z));
    }
    void flag(uint8_t f, bool on) { p = uint8_t(on ? (p | f) : (p & ~f)); }

    // Addressing modes -> effective address.
    uint16_t zp() { return fetch(); }
    uint16_t zpx() { return uint8_t(fetch() + x); }
    uint16_t zpy() { return uint8_t(fetch() + y); }
    uint16_t abs_() { return fetch16(); }
    uint16_t absx() { return uint16_t(fetch16() + x); }
    uint16_t absy() { return uint16_t(fetch16() + y); }
    uint16_t indx() {
        const uint8_t z = uint8_t(fetch() + x);
        return uint16_t(mem[z] | (mem[uint8_t(z + 1)] << 8));
    }
    uint16_t indy() {
        const uint8_t z = fetch();
        return uint16_t((mem[z] | (mem[uint8_t(z + 1)] << 8)) + y);
    }

    void adc(uint8_t v) {
        if (p & D) {   // decimal mode (NMOS semantics, flags approximate)
            int lo = (a & 15) + (v & 15) + (p & C);
            int hi = (a >> 4) + (v >> 4);
            if (lo > 9) { lo += 6; ++hi; }
            if (hi > 9) hi += 6;
            const uint8_t r = uint8_t((hi << 4) | (lo & 15));
            flag(C, hi > 15);
            nz(r);
            a = r;
            return;
        }
        const unsigned s = unsigned(a) + v + (p & C);
        flag(V, (~(a ^ v) & (a ^ s) & 0x80) != 0);
        flag(C, s > 0xFF);
        a = uint8_t(s);
        nz(a);
    }
    void sbc(uint8_t v) {
        if (p & D) {
            int lo = (a & 15) - (v & 15) - (1 - (p & C));
            int hi = (a >> 4) - (v >> 4);
            if (lo < 0) { lo -= 6; --hi; }
            if (hi < 0) hi -= 6;
            const unsigned s = unsigned(a) - v - (1 - (p & C));
            flag(C, s < 0x100);
            a = uint8_t((hi << 4) | (lo & 15));
            nz(a);
            return;
        }
        adc(uint8_t(~v));
    }
    void cmp(uint8_t r, uint8_t v) {
        flag(C, r >= v);
        nz(uint8_t(r - v));
    }
    uint8_t asl(uint8_t v) { flag(C, v & 0x80); v = uint8_t(v << 1); nz(v); return v; }
    uint8_t lsr(uint8_t v) { flag(C, v & 1); v = uint8_t(v >> 1); nz(v); return v; }
    uint8_t rol(uint8_t v) { const bool c = p & C; flag(C, v & 0x80); v = uint8_t((v << 1) | c); nz(v); return v; }
    uint8_t ror(uint8_t v) { const bool c = p & C; flag(C, v & 1); v = uint8_t((v >> 1) | (c ? 0x80 : 0)); nz(v); return v; }
    void bit(uint8_t v) {
        flag(Z, (a & v) == 0);
        flag(N, v & 0x80);
        flag(V, v & 0x40);
    }
    void branch(bool take) {
        const int8_t off = int8_t(fetch());
        if (take) {
            pc = uint16_t(pc + off);
            ++cycles;
        }
    }
    template <typename F>
    void rmw(uint16_t ad, F f) { wr(ad, f(rd(ad))); }

    // NMOS base cycles per opcode (page crossings not counted; a taken
    // branch adds one).
    static constexpr uint8_t kCycles[256] = {
        7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
        6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
        6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
        6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
        2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
        2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
        2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
        2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    };
    void step() {
        const uint8_t op = fetch();
        cycles += kCycles[op];
        switch (op) {
            // Loads / stores
            case 0xA9: a = fetch(); nz(a); break;
            case 0xA5: a = rd(zp()); nz(a); break;
            case 0xB5: a = rd(zpx()); nz(a); break;
            case 0xAD: a = rd(abs_()); nz(a); break;
            case 0xBD: a = rd(absx()); nz(a); break;
            case 0xB9: a = rd(absy()); nz(a); break;
            case 0xA1: a = rd(indx()); nz(a); break;
            case 0xB1: a = rd(indy()); nz(a); break;
            case 0xA2: x = fetch(); nz(x); break;
            case 0xA6: x = rd(zp()); nz(x); break;
            case 0xB6: x = rd(zpy()); nz(x); break;
            case 0xAE: x = rd(abs_()); nz(x); break;
            case 0xBE: x = rd(absy()); nz(x); break;
            case 0xA0: y = fetch(); nz(y); break;
            case 0xA4: y = rd(zp()); nz(y); break;
            case 0xB4: y = rd(zpx()); nz(y); break;
            case 0xAC: y = rd(abs_()); nz(y); break;
            case 0xBC: y = rd(absx()); nz(y); break;
            case 0x85: wr(zp(), a); break;
            case 0x95: wr(zpx(), a); break;
            case 0x8D: wr(abs_(), a); break;
            case 0x9D: wr(absx(), a); break;
            case 0x99: wr(absy(), a); break;
            case 0x81: wr(indx(), a); break;
            case 0x91: wr(indy(), a); break;
            case 0x86: wr(zp(), x); break;
            case 0x96: wr(zpy(), x); break;
            case 0x8E: wr(abs_(), x); break;
            case 0x84: wr(zp(), y); break;
            case 0x94: wr(zpx(), y); break;
            case 0x8C: wr(abs_(), y); break;
            // Transfers
            case 0xAA: x = a; nz(x); break;
            case 0xA8: y = a; nz(y); break;
            case 0x8A: a = x; nz(a); break;
            case 0x98: a = y; nz(a); break;
            case 0xBA: x = sp; nz(x); break;
            case 0x9A: sp = x; break;
            // Stack
            case 0x48: push(a); break;
            case 0x68: a = pull(); nz(a); break;
            case 0x08: push(uint8_t(p | B | U)); break;
            case 0x28: p = uint8_t(pull() | U); break;
            // Logic / arithmetic
            case 0x29: a &= fetch(); nz(a); break;
            case 0x25: a &= rd(zp()); nz(a); break;
            case 0x35: a &= rd(zpx()); nz(a); break;
            case 0x2D: a &= rd(abs_()); nz(a); break;
            case 0x3D: a &= rd(absx()); nz(a); break;
            case 0x39: a &= rd(absy()); nz(a); break;
            case 0x21: a &= rd(indx()); nz(a); break;
            case 0x31: a &= rd(indy()); nz(a); break;
            case 0x09: a |= fetch(); nz(a); break;
            case 0x05: a |= rd(zp()); nz(a); break;
            case 0x15: a |= rd(zpx()); nz(a); break;
            case 0x0D: a |= rd(abs_()); nz(a); break;
            case 0x1D: a |= rd(absx()); nz(a); break;
            case 0x19: a |= rd(absy()); nz(a); break;
            case 0x01: a |= rd(indx()); nz(a); break;
            case 0x11: a |= rd(indy()); nz(a); break;
            case 0x49: a ^= fetch(); nz(a); break;
            case 0x45: a ^= rd(zp()); nz(a); break;
            case 0x55: a ^= rd(zpx()); nz(a); break;
            case 0x4D: a ^= rd(abs_()); nz(a); break;
            case 0x5D: a ^= rd(absx()); nz(a); break;
            case 0x59: a ^= rd(absy()); nz(a); break;
            case 0x41: a ^= rd(indx()); nz(a); break;
            case 0x51: a ^= rd(indy()); nz(a); break;
            case 0x69: adc(fetch()); break;
            case 0x65: adc(rd(zp())); break;
            case 0x75: adc(rd(zpx())); break;
            case 0x6D: adc(rd(abs_())); break;
            case 0x7D: adc(rd(absx())); break;
            case 0x79: adc(rd(absy())); break;
            case 0x61: adc(rd(indx())); break;
            case 0x71: adc(rd(indy())); break;
            case 0xE9: case 0xEB: sbc(fetch()); break;
            case 0xE5: sbc(rd(zp())); break;
            case 0xF5: sbc(rd(zpx())); break;
            case 0xED: sbc(rd(abs_())); break;
            case 0xFD: sbc(rd(absx())); break;
            case 0xF9: sbc(rd(absy())); break;
            case 0xE1: sbc(rd(indx())); break;
            case 0xF1: sbc(rd(indy())); break;
            case 0xC9: cmp(a, fetch()); break;
            case 0xC5: cmp(a, rd(zp())); break;
            case 0xD5: cmp(a, rd(zpx())); break;
            case 0xCD: cmp(a, rd(abs_())); break;
            case 0xDD: cmp(a, rd(absx())); break;
            case 0xD9: cmp(a, rd(absy())); break;
            case 0xC1: cmp(a, rd(indx())); break;
            case 0xD1: cmp(a, rd(indy())); break;
            case 0xE0: cmp(x, fetch()); break;
            case 0xE4: cmp(x, rd(zp())); break;
            case 0xEC: cmp(x, rd(abs_())); break;
            case 0xC0: cmp(y, fetch()); break;
            case 0xC4: cmp(y, rd(zp())); break;
            case 0xCC: cmp(y, rd(abs_())); break;
            case 0x24: bit(rd(zp())); break;
            case 0x2C: bit(rd(abs_())); break;
            // Increments
            case 0xE6: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { v++; nz(v); return v; }); break; }
            case 0xF6: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { v++; nz(v); return v; }); break; }
            case 0xEE: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { v++; nz(v); return v; }); break; }
            case 0xFE: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { v++; nz(v); return v; }); break; }
            case 0xC6: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { v--; nz(v); return v; }); break; }
            case 0xD6: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { v--; nz(v); return v; }); break; }
            case 0xCE: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { v--; nz(v); return v; }); break; }
            case 0xDE: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { v--; nz(v); return v; }); break; }
            case 0xE8: x++; nz(x); break;
            case 0xC8: y++; nz(y); break;
            case 0xCA: x--; nz(x); break;
            case 0x88: y--; nz(y); break;
            // Shifts
            case 0x0A: a = asl(a); break;
            case 0x4A: a = lsr(a); break;
            case 0x2A: a = rol(a); break;
            case 0x6A: a = ror(a); break;
            case 0x06: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { return asl(v); }); break; }
            case 0x16: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { return asl(v); }); break; }
            case 0x0E: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { return asl(v); }); break; }
            case 0x1E: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { return asl(v); }); break; }
            case 0x46: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { return lsr(v); }); break; }
            case 0x56: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { return lsr(v); }); break; }
            case 0x4E: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { return lsr(v); }); break; }
            case 0x5E: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { return lsr(v); }); break; }
            case 0x26: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { return rol(v); }); break; }
            case 0x36: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { return rol(v); }); break; }
            case 0x2E: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { return rol(v); }); break; }
            case 0x3E: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { return rol(v); }); break; }
            case 0x66: { const uint16_t ad = zp(); rmw(ad, [&](uint8_t v) { return ror(v); }); break; }
            case 0x76: { const uint16_t ad = zpx(); rmw(ad, [&](uint8_t v) { return ror(v); }); break; }
            case 0x6E: { const uint16_t ad = abs_(); rmw(ad, [&](uint8_t v) { return ror(v); }); break; }
            case 0x7E: { const uint16_t ad = absx(); rmw(ad, [&](uint8_t v) { return ror(v); }); break; }
            // Jumps, branches
            case 0x4C: pc = abs_(); break;
            case 0x6C: {
                const uint16_t ad = abs_();
                pc = uint16_t(mem[ad] | (mem[(ad & 0xFF00) | uint8_t(ad + 1)] << 8));   // page-wrap bug
                break;
            }
            case 0x20: {
                const uint16_t t = abs_();
                const uint16_t ret = uint16_t(pc - 1);
                push(uint8_t(ret >> 8));
                push(uint8_t(ret));
                pc = t;
                break;
            }
            case 0x60: {
                const uint16_t lo = pull();
                pc = uint16_t(((pull() << 8) | lo) + 1);
                break;
            }
            case 0x40: {
                p = uint8_t(pull() | U);
                const uint16_t lo = pull();
                pc = uint16_t((pull() << 8) | lo);
                break;
            }
            case 0x10: branch(!(p & N)); break;
            case 0x30: branch(p & N); break;
            case 0x50: branch(!(p & V)); break;
            case 0x70: branch(p & V); break;
            case 0x90: branch(!(p & C)); break;
            case 0xB0: branch(p & C); break;
            case 0xD0: branch(!(p & Z)); break;
            case 0xF0: branch(p & Z); break;
            // Flags
            case 0x18: flag(C, false); break;
            case 0x38: flag(C, true); break;
            case 0x58: flag(I, false); break;
            case 0x78: flag(I, true); break;
            case 0xB8: flag(V, false); break;
            case 0xD8: flag(D, false); break;
            case 0xF8: flag(D, true); break;
            case 0xEA: break;
            default:
                std::fprintf(stderr, "sidtrace: unsupported opcode $%02X at $%04X\n", op, unsigned(pc - 1));
                bad = true;
                break;
        }
    }

    // JSR to `addr` with A = acc; returns false on a bad opcode or runaway.
    bool call(uint16_t addr, uint8_t acc) {
        constexpr uint16_t kHalt = 0xFFF0;   // RTS target sentinel
        a = acc;
        x = y = 0;
        sp = 0xFD;
        push(uint8_t((kHalt - 1) >> 8));
        push(uint8_t(kHalt - 1));
        pc = addr;
        for (uint32_t n = 0; n < 2000000; ++n) {
            if (pc == kHalt) return true;
            step();
            if (bad) return false;
        }
        std::fprintf(stderr, "sidtrace: routine at $%04X did not return\n", unsigned(addr));
        return false;
    }
};

}  // namespace oracle
