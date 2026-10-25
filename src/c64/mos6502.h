// SIDTracker -- MOS 6502 / 6510 core (NMOS, documented opcodes) for the
// SID player (src/c64/sid_player.h) and the oracle tools. MIT, SIDTracker.
//
// Memory: `ram` is the C64's 64 KB; zero page, stack and $0000-$9FFF are
// read from it directly, $A000-$FFFF and the I/O area go through `bus`
// (ROM banking, I/O). Writes below $D000 and from $E000 land in RAM (as on a
// C64, writes "under" ROM go to RAM); $D000-$DFFF writes go through `bus`.
// The stable undocumented NMOS opcodes (LAX, SAX, DCP, ISC, SLO, RLA, SRE,
// RRA, ANC, ALR, ARR, SBX, SHX, SHY, multi-byte NOPs) are implemented; the unstable
// rest and the JAM opcodes stop the CPU (`bad`).
#pragma once

#include <cstdint>

namespace sidt {
namespace c64 {

class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t read(uint16_t addr) = 0;               // $A000-$FFFF
    virtual void write(uint16_t addr, uint8_t v) = 0;      // $D000-$DFFF
};

class Mos6502 {
public:
    uint8_t* ram = nullptr;   // 64 KB
    Bus* bus = nullptr;
    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, sp = 0xFD, p = 0x24;
    uint64_t cycles = 0;
    bool bad = false;
    uint8_t badOpcode = 0;    // the opcode that stopped the CPU

    enum : uint8_t { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };

    uint8_t rd(uint16_t ad) { return ad < 0xA000 ? ram[ad] : bus->read(ad); }
    void wr(uint16_t ad, uint8_t v) {
        if (ad >= 0xD000 && ad < 0xE000) bus->write(ad, v);
        else ram[ad] = v;
    }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() {
        const uint16_t lo = fetch();
        return uint16_t(lo | (fetch() << 8));
    }
    void push(uint8_t v) { ram[0x100 + sp--] = v; }
    uint8_t pull() { return ram[0x100 + ++sp]; }
    void nz(uint8_t v) { p = uint8_t((p & ~(N | Z)) | (v & N) | (v ? 0 : Z)); }
    void flag(uint8_t f, bool on) { p = uint8_t(on ? (p | f) : (p & ~f)); }

    uint16_t zp() { return fetch(); }
    uint16_t zpx() { return uint8_t(fetch() + x); }
    uint16_t zpy() { return uint8_t(fetch() + y); }
    uint16_t abs_() { return fetch16(); }
    uint16_t absx() { return uint16_t(fetch16() + x); }
    uint16_t absy() { return uint16_t(fetch16() + y); }
    uint16_t indx() {
        const uint8_t z = uint8_t(fetch() + x);
        return uint16_t(ram[z] | (ram[uint8_t(z + 1)] << 8));
    }
    uint16_t indy() {
        const uint8_t z = fetch();
        return uint16_t((ram[z] | (ram[uint8_t(z + 1)] << 8)) + y);
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
    void bitTest(uint8_t v) {
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
            case 0x24: bitTest(rd(zp())); break;
            case 0x2C: bitTest(rd(abs_())); break;
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
                pc = uint16_t(rd(ad) | (rd(uint16_t((ad & 0xFF00) | uint8_t(ad + 1))) << 8));   // page-wrap bug
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
            // --- stable undocumented NMOS opcodes (used by some players) ---
            // SAX: store A & X
            case 0x87: wr(zp(), uint8_t(a & x)); break;
            case 0x97: wr(zpy(), uint8_t(a & x)); break;
            case 0x8F: wr(abs_(), uint8_t(a & x)); break;
            case 0x83: wr(indx(), uint8_t(a & x)); break;
            // LAX: load A and X
            case 0xA7: a = x = rd(zp()); nz(a); break;
            case 0xB7: a = x = rd(zpy()); nz(a); break;
            case 0xAF: a = x = rd(abs_()); nz(a); break;
            case 0xBF: a = x = rd(absy()); nz(a); break;
            case 0xA3: a = x = rd(indx()); nz(a); break;
            case 0xB3: a = x = rd(indy()); nz(a); break;
            case 0xAB: a = x = uint8_t((a | 0xEE) & fetch()); nz(a); break;   // LXA (magic $EE)
            // Read-modify-write + ALU combinations.
            case 0xC7: case 0xD7: case 0xCF: case 0xDF: case 0xDB: case 0xC3: case 0xD3:   // DCP
            case 0xE7: case 0xF7: case 0xEF: case 0xFF: case 0xFB: case 0xE3: case 0xF3:   // ISC
            case 0x07: case 0x17: case 0x0F: case 0x1F: case 0x1B: case 0x03: case 0x13:   // SLO
            case 0x27: case 0x37: case 0x2F: case 0x3F: case 0x3B: case 0x23: case 0x33:   // RLA
            case 0x47: case 0x57: case 0x4F: case 0x5F: case 0x5B: case 0x43: case 0x53:   // SRE
            case 0x67: case 0x77: case 0x6F: case 0x7F: case 0x7B: case 0x63: case 0x73: { // RRA
                uint16_t ad;
                switch (op & 0x1F) {
                    case 0x07: ad = zp(); break;
                    case 0x17: ad = zpx(); break;
                    case 0x0F: ad = abs_(); break;
                    case 0x1F: ad = absx(); break;
                    case 0x1B: ad = absy(); break;
                    case 0x03: ad = indx(); break;
                    default: ad = indy(); break;
                }
                uint8_t v = rd(ad);
                switch (op & 0xE0) {
                    case 0xC0: v--; wr(ad, v); cmp(a, v); break;              // DCP
                    case 0xE0: v++; wr(ad, v); sbc(v); break;                 // ISC
                    case 0x00: v = asl(v); wr(ad, v); a |= v; nz(a); break;   // SLO
                    case 0x20: v = rol(v); wr(ad, v); a &= v; nz(a); break;   // RLA
                    case 0x40: v = lsr(v); wr(ad, v); a ^= v; nz(a); break;   // SRE
                    default: v = ror(v); wr(ad, v); adc(v); break;            // RRA
                }
                break;
            }
            case 0x0B: case 0x2B: a &= fetch(); nz(a); flag(C, a & 0x80); break;   // ANC
            case 0x4B: a &= fetch(); a = lsr(a); break;                            // ALR
            case 0x6B: {                                                           // ARR
                a &= fetch();
                a = uint8_t((a >> 1) | ((p & C) ? 0x80 : 0));
                nz(a);
                flag(C, a & 0x40);
                flag(V, ((a >> 6) ^ (a >> 5)) & 1);
                break;
            }
            case 0xCB: {                                                           // SBX (AXS)
                const uint8_t v = fetch();
                const uint8_t t = uint8_t(a & x);
                flag(C, t >= v);
                x = uint8_t(t - v);
                nz(x);
                break;
            }
            // NOPs with operands.
            case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: break;
            case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: fetch(); break;
            case 0x04: case 0x44: case 0x64: (void)rd(zp()); break;
            case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: (void)rd(zpx()); break;
            case 0x0C: (void)rd(abs_()); break;
            case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: (void)rd(absx()); break;
            case 0x9C: {                                                           // SHY abs,X
                const uint16_t base = fetch16();
                const uint16_t ad = uint16_t(base + x);
                wr(ad, uint8_t(y & ((base >> 8) + 1)));
                break;
            }
            case 0x9E: {                                                           // SHX abs,Y
                const uint16_t base = fetch16();
                const uint16_t ad = uint16_t(base + y);
                wr(ad, uint8_t(x & ((base >> 8) + 1)));
                break;
            }
            case 0x00: {                                                           // BRK
                const uint16_t ret = uint16_t(pc + 1);
                push(uint8_t(ret >> 8));
                push(uint8_t(ret));
                push(uint8_t(p | B | U));
                flag(I, true);
                pc = uint16_t(rd(0xFFFE) | (rd(0xFFFF) << 8));
                break;
            }
            default:
                bad = true;
                badOpcode = op;
                break;
        }
    }

    // JSR to `addr` with A = acc and run until it returns (RTS / RTI to the
    // sentinel). False on an undocumented opcode or after `limit`
    // instructions (a tune that never returns).
    bool call(uint16_t addr, uint8_t acc, uint32_t limit = 2000000) {
        constexpr uint16_t kHalt = 0xFFF0;   // return-address sentinel (never executed)
        a = acc;
        x = y = 0;
        sp = 0xFD;
        push(uint8_t((kHalt - 1) >> 8));
        push(uint8_t(kHalt - 1));
        pc = addr;
        for (uint32_t n = 0; n < limit; ++n) {
            if (pc == kHalt) return true;
            step();
            if (bad) return false;
        }
        return false;
    }
    // An interrupt into `vector` (as the CPU does: PC and P pushed), run
    // until its RTI returns to the sentinel.
    bool irq(uint16_t vector, uint32_t limit = 2000000) {
        constexpr uint16_t kHalt = 0xFFF0;
        sp = 0xFD;
        push(uint8_t(kHalt >> 8));
        push(uint8_t(kHalt));
        push(uint8_t((p | U) & ~B));
        flag(I, true);
        pc = vector;
        for (uint32_t n = 0; n < limit; ++n) {
            if (pc == kHalt) return true;
            step();
            if (bad) return false;
        }
        return false;
    }
};

}  // namespace c64
}  // namespace sidt
