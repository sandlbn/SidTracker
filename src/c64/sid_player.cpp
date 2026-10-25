// SIDTracker -- SID player (sid_player.h). PSID header: the PSID / RSID
// file format description (HVSC, "SID_file_format.txt").
#include "c64/sid_player.h"

#include <cstring>
#include <initializer_list>

namespace sidt {
namespace c64 {

namespace {
uint16_t be16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
void str32(char* out, const uint8_t* p) {
    for (int i = 0; i < 32; ++i) out[i] = char(p[i]);
    out[32] = 0;
}
constexpr uint32_t kFrameLimit = 300000;   // instructions per call: more = hung
}  // namespace

void SidPlayer::setMemory(uint8_t* ram, uint8_t* rom) {
    ram_ = ram;
    rom_ = rom;
    cpu_.ram = ram;
    cpu_.bus = this;
}

void SidPlayer::buildRom() {
    // A KERNAL that only knows interrupts: RTS everywhere, the IRQ entry
    // pushes A / X / Y and jumps through ($0314), the exits pull them and RTI.
    std::memset(rom_, 0x60, kRomBytes);
    auto put = [&](uint16_t at, std::initializer_list<uint8_t> b) {
        uint16_t a = at;
        for (uint8_t v : b) rom_[(a++) - 0xE000] = v;
    };
    put(0xFF48, {0x48, 0x8A, 0x48, 0x98, 0x48, 0x6C, 0x14, 0x03});   // PHA TXA PHA TYA PHA JMP ($0314)
    // The KERNAL IRQ tail: entries anywhere in $EA31-$EA80 ($EA31, $EA7E, ...)
    // slide through NOPs to the exit at $EA81.
    for (uint16_t a = 0xEA31; a < 0xEA81; ++a) rom_[a - 0xE000] = 0xEA;
    put(0xEA81, {0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40});               // PLA TAY PLA TAX PLA RTI
    put(0xFE43, {0x40});                                             // NMI: RTI
    put(0xFFFA, {0x43, 0xFE, 0xE2, 0xFC, 0x48, 0xFF});               // NMI, RESET, IRQ vectors
}

void SidPlayer::setRamVectors(uint16_t load, uint32_t len) {
    // The KERNAL's RAM vectors, unless the tune's own data is there.
    if (load <= 0x0319 && load + len > 0x0314) return;
    ram_[0x0314] = 0x31;
    ram_[0x0315] = 0xEA;
    ram_[0x0318] = 0x43;
    ram_[0x0319] = 0xFE;
}

bool SidPlayer::load(const uint8_t* f, uint32_t size, SidInfo& info, const char*& err) {
    err = nullptr;
    if (!ram_ || !rom_) return err = "no memory", false;
    if (size < 0x76 + 2) return err = "not a .sid file", false;
    if (!std::memcmp(f, "RSID", 4)) return err = "RSID tunes need a full C64 (not yet)", false;
    if (std::memcmp(f, "PSID", 4)) return err = "not a .sid file", false;
    SidInfo s;
    s.version = be16(f + 4);
    const uint16_t off = be16(f + 6);
    s.load = be16(f + 8);
    s.init = be16(f + 10);
    s.play = be16(f + 12);
    s.songs = be16(f + 14);
    s.start = be16(f + 16);
    s.speed = uint32_t(f[0x12] << 24 | f[0x13] << 16 | f[0x14] << 8 | f[0x15]);
    str32(s.name, f + 0x16);
    str32(s.author, f + 0x36);
    str32(s.released, f + 0x56);
    if (s.version >= 2 && off >= 0x7C && size >= 0x7C) {
        const uint16_t flags = be16(f + 0x76);
        const int clock = (flags >> 2) & 3;
        s.ntsc = clock == 2;
        s.model[0] = uint8_t((flags >> 4) & 3);
        s.model[1] = uint8_t((flags >> 6) & 3);
        // Valid extra-SID addresses: $D420-$D7E0 or $DE00-$DFE0, even.
        auto sidAt = [](uint8_t a) -> uint16_t {
            return (!(a & 1) && ((a >= 0x42 && a <= 0x7E) || (a >= 0xE0 && a <= 0xFE))) ? uint16_t(0xD000 | (a << 4)) : 0;
        };
        if (s.version >= 3 && f[0x7A]) s.sid2 = sidAt(f[0x7A]);
        if (s.version >= 4 && f[0x7B]) s.sid3 = sidAt(f[0x7B]);
        if (!s.model[1]) s.model[1] = s.model[0];
    }
    if (off > size) return err = "bad header", false;
    const uint8_t* data = f + off;
    uint32_t len = size - off;
    if (!s.load) {
        if (len < 2) return err = "no data", false;
        s.load = uint16_t(data[0] | (data[1] << 8));
        data += 2;
        len -= 2;
    }
    if (!s.songs) s.songs = 1;
    if (!s.start || s.start > s.songs) s.start = 1;
    if (s.load + len > 0x10000u) return err = "data past $FFFF", false;
    if (!s.init) s.init = s.load;
    std::memset(ram_, 0, kRamBytes);
    std::memcpy(ram_ + s.load, data, len);
    buildRom();
    setRamVectors(s.load, len);
    if (!(s.load <= 0x02A6 && s.load + len > 0x02A6)) ram_[0x02A6] = s.ntsc ? 0 : 1;   // KERNAL PAL / NTSC flag
    info_ = s;
    info = s;
    return true;
}

uint8_t SidPlayer::bankFor(uint16_t addr) const {
    // PSID environment: the bank register as the address needs it.
    if (addr < 0xA000) return 0x37;
    if (addr < 0xD000) return 0x36;
    if (addr >= 0xE000) return 0x35;
    return 0x34;
}

bool SidPlayer::start(int song, SidWrite* out, int* n) {
    if (!ram_) return false;
    song_ = song < 1 ? 1 : (song > info_.songs ? info_.songs : song);
    ciaWritten_ = false;
    ciaLatch_ = info_.ntsc ? 0x4295 : 0x4025;   // the KERNAL's 60 Hz timer value; writes merge into it
    // Clear what an earlier song left (the tune's data stays loaded).
    ram_[0] = 0x2F;
    ram_[1] = bankFor(info_.init);
    out_ = out;
    n_ = 0;
    cpu_.bad = false;
    cpu_.p = 0x24;
    const bool ok = cpu_.call(info_.init, uint8_t(song_ - 1), kFrameLimit * 10);
    if (n) *n = n_;
    out_ = nullptr;
    if (!ok) return false;
    const uint32_t clock = clockHz();
    const int bit = song_ - 1 > 31 ? 31 : song_ - 1;
    const bool cia = (info_.speed >> bit) & 1;
    if (cia) {
        const uint32_t latch = ciaLatch_ ? ciaLatch_ : (info_.ntsc ? 0x4295 : 0x4025);
        rate_ = uint32_t(uint64_t(clock) * 1000u / (latch + 1u));
    } else {
        rate_ = info_.ntsc ? uint32_t(uint64_t(clock) * 1000u / 17095u) : uint32_t(uint64_t(clock) * 1000u / 19656u);
    }
    if (rate_ < 1000) rate_ = 1000;   // at least one call a second
    return true;
}

bool SidPlayer::frame(SidWrite* out, int& n) {
    out_ = out;
    n_ = 0;
    bool ok;
    if (info_.play) {
        ram_[1] = bankFor(info_.play);
        ok = cpu_.call(info_.play, 0, kFrameLimit);
    } else {
        // The init routine installed an interrupt handler.
        const bool kernal = ram_[1] & 2;
        const uint16_t vec = kernal ? 0xFF48 : uint16_t(ram_[0xFFFE] | (ram_[0xFFFF] << 8));
        ok = cpu_.irq(vec, kFrameLimit);
    }
    n = n_;
    out_ = nullptr;
    return ok;
}

void SidPlayer::record(uint8_t chip, uint8_t reg, uint8_t v) {
    if (!out_ || reg > 0x18) return;
    if (n_ < kMaxWrites) {
        out_[n_++] = {chip, reg, v};
        return;
    }
    for (int i = 0; i < n_; ++i)   // full: replace the register's earlier write
        if (out_[i].chip == chip && out_[i].reg == reg) {
            out_[i].val = v;
            return;
        }
}

uint8_t SidPlayer::read(uint16_t a) {
    const uint8_t bank = ram_[1] & 7;
    if (a >= 0xE000) return (bank & 2) ? rom_[a - 0xE000] : ram_[a];
    if (a >= 0xD000 && a < 0xE000 && (bank & 3) && (bank & 4)) {
        if (a >= 0xD400 && a < 0xD800) {
            const uint8_t r = a & 0x1F;
            if (r == 0x1B || r == 0x1C) {   // OSC3 / ENV3: noise is the common use
                rnd_ ^= rnd_ << 13;
                rnd_ ^= rnd_ >> 17;
                rnd_ ^= rnd_ << 5;
                return uint8_t(rnd_);
            }
            return 0;
        }
        if (a == 0xD012) {   // raster line, moving on at every read
            raster_ = uint16_t((raster_ + 1) % (info_.ntsc ? 263 : 312));
            return uint8_t(raster_);
        }
        if (a == 0xD011) {   // bit 7 = raster bit 8: moves on too, so polling loops end
            raster_ = uint16_t((raster_ + 1) % (info_.ntsc ? 263 : 312));
            return uint8_t(((raster_ >> 1) & 0x80) | 0x1B);
        }
        if (a == 0xDC04 || a == 0xDC05) {   // timer A counting down
            rnd_ = rnd_ * 1103515245u + 12345u;
            return uint8_t(rnd_ >> 16);
        }
        return 0;
    }
    return ram_[a];   // BASIC / character ROM areas: RAM (no ROM code)
}

void SidPlayer::write(uint16_t a, uint8_t v) {
    const uint8_t bank = ram_[1] & 7;
    if (!((bank & 3) && (bank & 4))) {
        ram_[a] = v;   // I/O banked out: RAM
        return;
    }
    if (info_.sid3 && (a & 0xFFE0) == info_.sid3) return;   // third SID: not emulated, keep it off SID 1
    if (info_.sid2 && (a & 0xFFE0) == info_.sid2) {
        record(1, uint8_t(a & 0x1F), v);
        return;
    }
    if (a >= 0xD400 && a < 0xD800) {
        record(0, uint8_t(a & 0x1F), v);
        return;
    }
    if (a == 0xDC04) {
        ciaLatch_ = uint16_t((ciaLatch_ & 0xFF00) | v);
        ciaWritten_ = true;
    } else if (a == 0xDC05) {
        ciaLatch_ = uint16_t((ciaLatch_ & 0x00FF) | (v << 8));
        ciaWritten_ = true;
    }
}

}  // namespace c64
}  // namespace sidt
