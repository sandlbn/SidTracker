// SIDTracker -- GoatTracker 2 raw file formats (gt_format.h).
#include "compatibility/goat/gt_format.h"

#include <cstring>

namespace sidt {
namespace goat {

namespace {

class In {
public:
    explicit In(Bytes b) : b_(b) {}
    uint8_t u8() {
        if (pos_ < b_.size) return b_.data[pos_++];
        short_ = true;
        return 0;
    }
    void bytes(void* dst, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t*>(dst)[i] = u8();
    }
    bool isShort() const { return short_; }
    uint32_t pos() const { return pos_; }

private:
    Bytes b_;
    uint32_t pos_ = 0;
    bool short_ = false;
};

void readInstrument(In& in, Instrument& i, bool nameOnly16) {
    i.ad = in.u8();
    i.sr = in.u8();
    i.wavePtr = in.u8();
    i.pulsePtr = in.u8();
    i.filterPtr = in.u8();
    i.vibParam = in.u8();
    i.vibDelay = in.u8();
    i.gateTimer = in.u8();
    i.firstWave = in.u8();
    (void)nameOnly16;
    in.bytes(i.name, 16);
    i.name[16] = 0;
}

void readTables(In& in, Tables& t) {
    for (int k = 0; k < 4; ++k) {
        t.len[k] = in.u8();
        in.bytes(t.left[k], t.len[k]);
        in.bytes(t.right[k], t.len[k]);
    }
}

// Fixed-width, zero-padded text field (not necessarily terminated).
void field(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = 0;
}

bool putInstrument(Sink& s, const Instrument& i) {
    const uint8_t b[9] = {i.ad, i.sr, i.wavePtr, i.pulsePtr, i.filterPtr, i.vibParam, i.vibDelay, i.gateTimer,
                          i.firstWave};
    char name[16];
    field(name, i.name, 16);
    return s.put(b, 9) && s.put(name, 16);
}

bool putTables(Sink& s, const Tables& t) {
    for (int k = 0; k < 4; ++k) {
        if (!s.put(&t.len[k], 1) || !s.put(t.left[k], t.len[k]) || !s.put(t.right[k], t.len[k])) return false;
    }
    return true;
}

}  // namespace

const char* resultText(Result r) {
    switch (r) {
        case Result::kOk: return "ok";
        case Result::kTruncated: return "file is truncated";
        case Result::kBadMagic: return "not a GoatTracker 2 file";
        case Result::kOldFormat: return "older GoatTracker format: resave it in GoatTracker 2.59 or later";
        case Result::kCorrupt: return "file is damaged";
        case Result::kTooLarge: return "file exceeds GoatTracker limits";
    }
    return "?";
}

Result readSong(Bytes b, Song& s) {
    s = Song{};
    if (b.size < 4 || std::memcmp(b.data, "GTS", 3) != 0) return Result::kBadMagic;
    if (b.data[3] != '5') return Result::kOldFormat;
    In in(b);
    uint8_t magic[4];
    in.bytes(magic, 4);
    in.bytes(s.name, kNameLen);
    in.bytes(s.author, kNameLen);
    in.bytes(s.copyright, kNameLen);
    s.subtunes = in.u8();
    if (!s.subtunes || s.subtunes > kSubtunes) return Result::kCorrupt;
    for (int st = 0; st < s.subtunes; ++st)
        for (int ch = 0; ch < 3; ++ch) {
            // Length byte counts the order bytes including the RST mark;
            // the restart position follows it (checked against GT files).
            const uint8_t n = in.u8();
            if (!n) return Result::kCorrupt;
            in.bytes(s.order[st][ch], n);
            s.orderLen[st][ch] = n;
            s.orderRestart[st][ch] = in.u8();
            if (s.order[st][ch][n - 1] != kOrderEnd) return Result::kCorrupt;
        }
    s.instruments = in.u8();
    if (s.instruments >= kInstruments) return Result::kTooLarge;
    for (int i = 1; i <= s.instruments; ++i) readInstrument(in, s.inst[i], true);
    readTables(in, s.tables);
    s.patterns = in.u8();
    if (s.patterns > kPatterns) return Result::kTooLarge;
    for (int p = 0; p < s.patterns; ++p) {
        const uint8_t m = in.u8();   // rows including the end-mark row
        if (!m || m > kPatternRows + 1) return Result::kCorrupt;
        for (int r = 0; r < m; ++r) {
            Row row;
            row.note = in.u8();
            row.inst = in.u8();
            row.cmd = in.u8();
            row.param = in.u8();
            if (r < kPatternRows) s.pattern[p][r] = row;
            if (r == m - 1 && row.note != kPatternEnd) return Result::kCorrupt;
        }
        s.rows[p] = uint8_t(m - 1);
    }
    if (in.isShort()) return Result::kTruncated;
    return Result::kOk;
}

bool writeSong(const Song& s, Sink& out) {
    char hdr[4 + 3 * kNameLen] = {'G', 'T', 'S', '5'};
    field(hdr + 4, s.name, kNameLen);
    field(hdr + 4 + kNameLen, s.author, kNameLen);
    field(hdr + 4 + 2 * kNameLen, s.copyright, kNameLen);
    if (!out.put(hdr, sizeof hdr) || !out.put(&s.subtunes, 1)) return false;
    for (int st = 0; st < s.subtunes; ++st)
        for (int ch = 0; ch < 3; ++ch) {
            const uint8_t n = s.orderLen[st][ch];
            if (!out.put(&n, 1) || !out.put(s.order[st][ch], n) || !out.put(&s.orderRestart[st][ch], 1)) return false;
        }
    if (!out.put(&s.instruments, 1)) return false;
    for (int i = 1; i <= s.instruments; ++i)
        if (!putInstrument(out, s.inst[i])) return false;
    if (!putTables(out, s.tables) || !out.put(&s.patterns, 1)) return false;
    for (int p = 0; p < s.patterns; ++p) {
        const uint8_t m = uint8_t(s.rows[p] + 1);
        if (!out.put(&m, 1)) return false;
        for (int r = 0; r < s.rows[p]; ++r) {
            const Row& row = s.pattern[p][r];
            const uint8_t b[4] = {row.note, row.inst, row.cmd, row.param};
            if (!out.put(b, 4)) return false;
        }
        const uint8_t end[4] = {kPatternEnd, 0, 0, 0};
        if (!out.put(end, 4)) return false;
    }
    return true;
}

Result readIns(Bytes b, InsFile& f) {
    f = InsFile{};
    if (b.size < 4 || std::memcmp(b.data, "GTI", 3) != 0) return Result::kBadMagic;
    if (b.data[3] != '5') return Result::kOldFormat;
    In in(b);
    uint8_t magic[4];
    in.bytes(magic, 4);
    readInstrument(in, f.inst, true);
    readTables(in, f.tables);
    return in.isShort() ? Result::kTruncated : Result::kOk;
}

bool writeIns(const InsFile& f, Sink& out) {
    return out.put("GTI5", 4) && putInstrument(out, f.inst) && putTables(out, f.tables);
}

}  // namespace goat
}  // namespace sidt
