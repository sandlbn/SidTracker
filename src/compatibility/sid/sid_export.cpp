// SIDTracker -- .sid export (sid_export.h). Song area layout: player.s.
#include "compatibility/sid/sid_export.h"

#include <cstdio>
#include <cstring>

#include "compatibility/sid/player_bin.h"

namespace sidt {
namespace psid {

namespace {

constexpr uint32_t kArea = kSidPlayer_STREAMS - kSidPlayer_songarea;   // fixed song area
constexpr uint32_t kMaxImage = 0xD000 - kLoadAddress;
static_assert(kSidPlayerBase == kLoadAddress, "player.s .org");

// A cell as the player reads it: only what affects playback.
Cell normal(const Cell& c) {
    Cell n;
    if (note::isNote(c.note) || c.note == note::kKeyOn || c.note == note::kKeyOff) n.note = c.note;
    if (c.inst < kInstrumentCount) n.inst = c.inst;
    n.cmd = uint8_t(c.cmd & 15);
    if (n.cmd) n.param = c.param;
    return n;
}

void referenced(const Song& s, bool used[kPatternCount]) {
    std::memset(used, 0, kPatternCount);
    const int len = s.orderLen ? s.orderLen : 1;
    for (int i = 0; i < len; ++i) used[s.order[i].pattern & (kPatternCount - 1)] = true;
}

bool usesSid2(const Song& s, const bool* usedIn) {
    bool own[kPatternCount];
    const bool* used = usedIn;
    if (!used) {
        referenced(s, own);
        used = own;
    }
    for (int p = 0; p < kPatternCount; ++p) {
        if (!used[p]) continue;
        const Pattern& pt = s.pattern(p);
        for (int r = 0; r < pt.rows; ++r)
            for (int ch = 3; ch < kChannels; ++ch)
                if (!normal(pt.cells[r][ch]).empty()) return true;
    }
    return false;
}

// Encodes one track of a pattern (player.s "Pattern stream"); returns bytes.
uint32_t encodeTrack(const Pattern& p, int ch, uint8_t* out) {
    uint32_t n = 0;
    int run = 0;
    auto flush = [&] {
        while (run > 0) {
            const int k = run > 32 ? 32 : run;
            out[n++] = uint8_t(k - 1);
            run -= k;
        }
    };
    for (int r = 0; r < p.rows; ++r) {
        const Cell c = normal(p.cells[r][ch]);
        if (c.empty()) {
            ++run;
            continue;
        }
        flush();
        const uint8_t f = uint8_t((c.note ? 0x80 : 0) | (c.inst ? 0x40 : 0) | (c.cmd ? 0x20 : 0));
        out[n++] = f;
        if (c.note) out[n++] = c.note;
        if (c.inst) out[n++] = c.inst;
        if (c.cmd) {
            out[n++] = c.cmd;
            out[n++] = c.param;
        }
    }
    flush();
    return n;
}

void putStr(uint8_t* d, const char* s) {
    for (int i = 0; i < 32 && s[i]; ++i) d[i] = uint8_t(s[i]);
}

void be16(uint8_t* d, uint16_t v) {
    d[0] = uint8_t(v >> 8);
    d[1] = uint8_t(v);
}

bool frameRate(const Song& s) {
    return (s.sid.clock == SidClock::kPal && s.bpm == 125) || (s.sid.clock == SidClock::kNtsc && s.bpm == 150);
}

// Builds the C64 image (code + song area + streams) into img; returns size.
struct Done {
    uint32_t off, len;
};

uint32_t buildImage(const Song& s, uint8_t* img, uint32_t cap, Done* done, bool& dual, goat::Report& rep) {
    bool used[kPatternCount];
    referenced(s, used);
    dual = usesSid2(s, used);
    const uint32_t streams = kSidPlayer_STREAMS - kLoadAddress;
    if (cap < streams) return 0;
    std::memset(img, 0, streams);
    std::memcpy(img, kSidPlayerCode, kSidPlayerSize);
    auto at = [&](uint16_t addr) { return img + (addr - kLoadAddress); };
    // Tables (index = 1-based row).
    const uint16_t tl[kTableKinds] = {kSidPlayer_WAVEL, kSidPlayer_PULSEL, kSidPlayer_FILTL, kSidPlayer_SPDL};
    const uint16_t tr[kTableKinds] = {kSidPlayer_WAVER, kSidPlayer_PULSER, kSidPlayer_FILTR, kSidPlayer_SPDR};
    for (int t = 0; t < kTableKinds; ++t)
        for (int row = 1; row <= kTableLen; ++row) {
            at(tl[t])[row] = s.tables[t].l(row);
            at(tr[t])[row] = s.tables[t].r(row);
        }
    for (int i = 0; i < kInstrumentCount; ++i) {
        const Instrument& in = s.inst[i];
        at(kSidPlayer_I_AD)[i] = in.ad;
        at(kSidPlayer_I_SR)[i] = in.sr;
        at(kSidPlayer_I_FW)[i] = in.firstWave;
        at(kSidPlayer_I_WP)[i] = in.wavePtr;
        at(kSidPlayer_I_PP)[i] = in.pulsePtr;
        at(kSidPlayer_I_FP)[i] = in.filterPtr;
        at(kSidPlayer_I_VP)[i] = in.vibParam;
        at(kSidPlayer_I_VD)[i] = in.vibDelay;
        at(kSidPlayer_I_GT)[i] = in.gateTimer;
    }
    const int len = s.orderLen ? s.orderLen : 1;
    for (int i = 0; i < len; ++i) {
        at(kSidPlayer_O_PAT)[i] = uint8_t(s.order[i].pattern & (kPatternCount - 1));
        at(kSidPlayer_O_TR)[i] = uint8_t(s.order[i].transpose);
        at(kSidPlayer_O_REP)[i] = s.order[i].repeat;
    }
    *at(kSidPlayer_H_ORDLEN) = uint8_t(len);
    *at(kSidPlayer_H_RESTART) = s.restartPos < len ? s.restartPos : 0;
    *at(kSidPlayer_H_SPEED) = s.speed;
    *at(kSidPlayer_H_HRAD) = s.hrAD;
    *at(kSidPlayer_H_HRSR) = s.hrSR;
    *at(kSidPlayer_H_NCH) = dual ? 6 : 3;
    *at(kSidPlayer_H_NCHIP) = dual ? 2 : 1;
    if (!frameRate(s)) {
        const uint32_t bpm = s.bpm >= 16 ? s.bpm : 125;
        const uint32_t timer = (s.sid.clockHz() * 5u + bpm) / (2u * bpm);   // clock / (bpm * 2 / 5)
        if (timer > 0xFFFF) {   // the CIA timer is 16 bits: below about 38 bpm a tick is too long
            rep.note("! tempo %u bpm too slow for a .sid (40 bpm or more)", unsigned(s.bpm));
            return 0;
        }
        *at(kSidPlayer_H_CIA) = 1;
        *at(kSidPlayer_H_CIALO) = uint8_t(timer);
        *at(kSidPlayer_H_CIAHI) = uint8_t(timer >> 8);
    }
    // Pattern streams, identical tracks shared.
    uint32_t end = streams;
    int ndone = 0;
    uint8_t buf[kMaxRows * 5 + 8];
    uint16_t first = 0;
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pt = s.pattern(p);
        at(kSidPlayer_P_ROWS)[p] = used[p] ? pt.rows : 1;
        at(kSidPlayer_P_STARTS)[p] = pt.starts;
        for (int ch = 0; ch < kChannels; ++ch) {
            uint16_t addr = first;
            if (used[p] && (ch < 3 || dual)) {
                const uint32_t n = encodeTrack(pt, ch, buf);
                int k = 0;
                while (k < ndone && !(done[k].len == n && !std::memcmp(img + done[k].off, buf, n))) ++k;
                if (k == ndone) {
                    if (end + n > cap || end + n > kMaxImage) {
                        rep.note("! too large for the C64: more than %u KB", unsigned(kMaxImage / 1024));
                        return 0;
                    }
                    std::memcpy(img + end, buf, n);
                    done[ndone++] = {end, n};
                    end += n;
                }
                addr = uint16_t(kLoadAddress + done[k].off);
                if (!first) first = addr;
            }
            at(kSidPlayer_P_LO)[ch * 128 + p] = uint8_t(addr);
            at(kSidPlayer_P_HI)[ch * 128 + p] = uint8_t(addr >> 8);
        }
    }
    return end;
}

}  // namespace

ExportCheck checkExport(const Song& s, uint8_t* work, uint32_t workBytes) {
    ExportCheck c;
    goat::Report& r = c.report;
    if (s.usesDrums()) r.note("! PCM drum column is not exported");
    if (s.fx.any()) r.note("! effects are not exported");
    if (s.mod.any()) r.note("! LFOs and voice pan are not exported");
    if (s.automatedLanes()) r.note("! Perform automation is not exported");
    goat::Report sizeRep;
    const uint32_t file = exportSid(s, work, workBytes, sizeRep);
    if (!file) {
        c.ok = false;
        for (int i = 0; i < sizeRep.lines; ++i) r.note("%s", sizeRep.line[i]);
        if (!sizeRep.lines) r.note("! no work memory");
        return c;
    }
    c.dual = usesSid2(s, nullptr);
    c.cia = !frameRate(s);
    c.bytes = file;
    const uint32_t n = file - 0x7E;
    r.note("%s, %s", c.dual ? "two SIDs (second at $D420)" : "one SID", s.sid.clock == SidClock::kNtsc ? "NTSC" : "PAL");
    if (c.cia) r.note("tempo %u bpm: CIA timer", unsigned(s.bpm));
    r.note("%u bytes, $%04X-$%04X", unsigned(c.bytes), unsigned(kLoadAddress), unsigned(kLoadAddress + n - 1));
    r.ok = true;
    return c;
}

uint32_t exportSid(const Song& s, uint8_t* out, uint32_t cap, goat::Report& rep) {
    if (!out || cap < kWorkBytes) return 0;
    Done* done = reinterpret_cast<Done*>(out + kTableOffset);
    bool dual = false;
    const uint32_t n = buildImage(s, out + 0x7E, kMaxFile - 0x7E, done, dual, rep);
    if (!n) return 0;
    uint8_t* h = out;
    std::memset(h, 0, 0x7E);
    std::memcpy(h, "PSID", 4);
    be16(h + 4, dual ? 3 : 2);
    be16(h + 6, 0x7C);
    be16(h + 8, 0);   // load address in the data
    be16(h + 10, kSidPlayer_init);
    be16(h + 12, kSidPlayer_play);
    be16(h + 14, 1);
    be16(h + 16, 1);
    h[0x15] = frameRate(s) ? 0 : 1;   // speed bit 0: CIA
    putStr(h + 0x16, s.name);
    putStr(h + 0x36, s.author);
    putStr(h + 0x56, "SIDTracker");
    auto model = [](sid::Model m) { return m == sid::Model::k6581 ? 1 : 2; };
    uint16_t flags = uint16_t((s.sid.clock == SidClock::kNtsc ? 2 : 1) << 2);
    flags = uint16_t(flags | model(s.sid.model[0]) << 4);
    if (dual) flags = uint16_t(flags | model(s.sid.model[1]) << 6);
    be16(h + 0x76, flags);
    h[0x7A] = dual ? 0x42 : 0;   // second SID at $D420
    h[0x7C] = uint8_t(kLoadAddress);
    h[0x7D] = uint8_t(kLoadAddress >> 8);
    rep.ok = true;
    return 0x7E + n;
}

}  // namespace psid
}  // namespace sidt
