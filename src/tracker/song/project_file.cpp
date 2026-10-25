// SIDTracker -- native project file (project_file.h).
#include "tracker/song/project_file.h"

#include <cstring>

namespace sidt {
namespace project {

namespace {

constexpr uint8_t kInstFields = 9;
constexpr uint32_t kMaxChunk = 1u << 20;

// Chunk payloads are assembled in a fixed buffer (largest: one pattern,
// 2 + 128 x 6 x 4 + 1 = 3075 bytes, or its automation, at most
// 2 + 128 x (3 + 16 x 2) + 1 = 4483 bytes).
class Buf {
public:
    void u8(uint8_t v) {
        if (n_ < sizeof b_) b_[n_++] = v;
        else over_ = true;
    }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void str(const char* s) {
        uint8_t len = 0;
        while (s[len] && len < kNameLen) ++len;
        u8(len);
        for (uint8_t i = 0; i < len; ++i) u8(uint8_t(s[i]));
    }
    bool flush(Writer& w, const char tag[4]) {
        if (over_) return false;
        uint8_t h[8];
        std::memcpy(h, tag, 4);
        for (int i = 0; i < 4; ++i) h[4 + i] = uint8_t(n_ >> (8 * i));
        const bool ok = w.write(h, 8) && (n_ == 0 || w.write(b_, n_));
        n_ = 0;
        return ok;
    }

private:
    uint8_t b_[8192];
    uint32_t n_ = 0;
    bool over_ = false;
};

// Bounds-checked payload cursor; reads past the end return 0 and mark bad.
class Cur {
public:
    Cur(const uint8_t* p, uint32_t n) : p_(p), n_(n) {}
    uint8_t u8() {
        if (i_ < n_) return p_[i_++];
        bad_ = true;
        return 0;
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        return uint16_t(lo | (u8() << 8));
    }
    void str(char* out) {
        const uint8_t len = u8();
        for (uint8_t i = 0; i < len; ++i) {
            const char c = char(u8());
            if (i < kNameLen) out[i] = c;
        }
        out[len < kNameLen ? len : kNameLen] = 0;
    }
    bool bad() const { return bad_; }
    uint32_t left() const { return n_ - i_; }

private:
    const uint8_t* p_;
    uint32_t n_;
    uint32_t i_ = 0;
    bool bad_ = false;
};

bool readAll(Reader& r, void* dst, uint32_t len) { return r.read(dst, len) == int32_t(len); }

}  // namespace

const char* resultText(Result r) {
    switch (r) {
        case Result::kOk: return "ok";
        case Result::kIoError: return "read/write error";
        case Result::kBadMagic: return "not a SIDTracker project";
        case Result::kNewerVersion: return "made by a newer SIDTracker";
        case Result::kCorrupt: return "file is damaged";
    }
    return "?";
}

Result save(const Song& s, Writer& w) {
    static Buf b;   // 8 KB: static, the main loop is the only writer
    const uint8_t head[8] = {'S', 'I', 'D', 'T', uint8_t(kFormatVersion), uint8_t(kFormatVersion >> 8), 0, 0};
    if (!w.write(head, sizeof head)) return Result::kIoError;

    b.str(s.name);
    b.str(s.author);
    b.u8(s.bpm);
    b.u8(s.speed);
    b.u8(s.hrAD);
    b.u8(s.hrSR);
    b.u8(uint8_t(s.mode));
    b.u8(s.restartPos);
    b.u8(s.multiSpeed());
    if (!b.flush(w, "META")) return Result::kIoError;

    b.u8(uint8_t(s.sid.model[0]));
    b.u8(uint8_t(s.sid.model[1]));
    b.u8(uint8_t(s.sid.clock));
    b.u8(s.sid.balance);
    if (!b.flush(w, "SIDC")) return Result::kIoError;

    b.u8(s.orderLen);
    for (int i = 0; i < s.orderLen; ++i) {
        b.u8(s.order[i].pattern);
        b.u8(uint8_t(s.order[i].transpose));
        b.u8(s.order[i].repeat);
    }
    if (!b.flush(w, "ORDR")) return Result::kIoError;

    bool referenced[kPatternCount] = {};
    for (int i = 0; i < s.orderLen; ++i) referenced[s.order[i].pattern & (kPatternCount - 1)] = true;
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pat = s.pattern(p);
        if (!referenced[p] && pat.empty() && pat.rows == kDefaultRows && pat.starts == 0x3F) continue;
        b.u8(uint8_t(p));
        b.u8(pat.rows);
        for (int r = 0; r < pat.rows; ++r)
            for (const Cell& c : pat.cells[r]) {
                b.u8(c.note);
                b.u8(c.inst);
                b.u8(c.cmd);
                b.u8(c.param);
            }
        b.u8(pat.starts);   // appended in 0.2: older readers ignore it
        if (!b.flush(w, "PATT")) return Result::kIoError;
    }

    // Slots without an INST record are init patches (0.8 on; older files
    // left silent slots out).
    if (!b.flush(w, "IDEF")) return Result::kIoError;
    // Slots a pattern plays are always written, so firmware before 0.8 (no
    // IDEF rule) plays them the same.
    bool played[kInstrumentCount] = {};
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pt = s.pattern(p);
        for (int r = 0; r < pt.rows; ++r)
            for (const Cell& c : pt.cells[r])
                if (c.inst < kInstrumentCount) played[c.inst] = true;
    }
    for (int i = 1; i < kInstrumentCount; ++i) {
        const Instrument& in = s.inst[i];
        Instrument def;
        def.clear(i);
        if (!played[i] && in.empty() && std::strcmp(in.name, def.name) == 0) continue;
        b.u8(uint8_t(i));
        b.u8(kInstFields);
        b.u8(in.ad);
        b.u8(in.sr);
        b.u8(in.firstWave);
        b.u8(in.wavePtr);
        b.u8(in.pulsePtr);
        b.u8(in.filterPtr);
        b.u8(in.vibParam);
        b.u8(in.vibDelay);
        b.u8(in.gateTimer);
        b.str(in.name);
        if (!b.flush(w, "INST")) return Result::kIoError;
    }

    // Perform automation (M7): pattern, rows, then per automated row: row,
    // lane mask (u16), the set lanes' values (s16, lane order); FF ends.
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pat = s.pattern(p);
        if (!pat.autoLanes()) continue;
        b.u8(uint8_t(p));
        b.u8(pat.rows);
        for (int r = 0; r < pat.rows; ++r) {
            if (!pat.autoSet[r]) continue;
            b.u8(uint8_t(r));
            b.u16(pat.autoSet[r]);
            for (int lane = 0; lane < kAutoLanes; ++lane)
                if ((pat.autoSet[r] >> lane) & 1) b.u16(uint16_t(pat.autoVal[r][lane]));
        }
        b.u8(0xFF);
        if (!b.flush(w, "AUTO")) return Result::kIoError;
    }

    // PCM drum column (M8): pattern, then (row, slot) pairs; FF ends.
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pat = s.pattern(p);
        bool any = false;
        for (int r = 0; r < pat.rows && !any; ++r) any = pat.drum[r] != 0;
        if (!any) continue;
        b.u8(uint8_t(p));
        for (int r = 0; r < pat.rows; ++r)
            if (pat.drum[r]) {
                b.u8(uint8_t(r));
                b.u8(pat.drum[r]);
            }
        b.u8(0xFF);
        if (!b.flush(w, "DRUM")) return Result::kIoError;
    }

    for (int k = 0; k < kTableKinds; ++k) {
        const Table& t = s.tables[k];
        const int used = t.used();
        if (!used) continue;
        b.u8(uint8_t(k));
        b.u8(uint8_t(used));
        for (int i = 0; i < used; ++i) b.u8(t.left[i]);
        for (int i = 0; i < used; ++i) b.u8(t.right[i]);
        if (!b.flush(w, "TABL")) return Result::kIoError;
    }

    // Extended settings (M8): field count, then the fields; later versions
    // append fields.
    {
        const audio::FxSettings& fx = s.fx;
        const ModSettings& m = s.mod;
        const uint8_t f[] = {fx.delayMix,   fx.delayTicks,      fx.delayFeedback,   fx.chorusMix,     fx.chorusRate,
                             fx.chorusDepth, fx.reverbMix,      fx.reverbSize,      fx.reverbDamp,    s.drumVolume,
                             m.lfo[0].target, m.lfo[0].shape,   m.lfo[0].period,    m.lfo[0].depth,   m.lfo[1].target,
                             m.lfo[1].shape,  m.lfo[1].period,  m.lfo[1].depth,     uint8_t(m.pan[0]), uint8_t(m.pan[1]),
                             uint8_t(m.pan[2]), uint8_t(m.pan[3]), uint8_t(m.pan[4]), uint8_t(m.pan[5])};
        b.u8(uint8_t(sizeof f));
        for (uint8_t v : f) b.u8(v);
        if (!b.flush(w, "EXTN")) return Result::kIoError;
    }

    if (s.mix.any()) {   // 0.8 on; older readers skip it
        for (uint8_t v : s.mix.volume) b.u8(v);
        if (!b.flush(w, "MIXR")) return Result::kIoError;
    }
    return b.flush(w, "END ") ? Result::kOk : Result::kIoError;
}

Result load(Song& s, Reader& r) {
    uint8_t head[8];
    if (!readAll(r, head, sizeof head)) return Result::kIoError;
    if (std::memcmp(head, "SIDT", 4) != 0) return Result::kBadMagic;
    const uint16_t version = uint16_t(head[4] | (head[5] << 8));
    if (version > kFormatVersion) return Result::kNewerVersion;
    s.clear();
    // The default song's instrument 1 and tables are not part of a file.
    bool loaded[kInstrumentCount] = {};
    bool initDefaults = false;
    s.inst[1].clear(1);
    for (Table& t : s.tables) t = Table{};
    static uint8_t payload[8192];
    for (;;) {
        uint8_t h[8];
        if (!readAll(r, h, sizeof h)) return Result::kCorrupt;
        const uint32_t len = uint32_t(h[4]) | uint32_t(h[5]) << 8 | uint32_t(h[6]) << 16 | uint32_t(h[7]) << 24;
        if (len > kMaxChunk) return Result::kCorrupt;
        if (std::memcmp(h, "END ", 4) == 0) break;
        if (len > sizeof payload) {   // unknown large chunk: skip it
            uint32_t left = len;
            while (left) {
                const uint32_t n = left < sizeof payload ? left : uint32_t(sizeof payload);
                if (!readAll(r, payload, n)) return Result::kCorrupt;
                left -= n;
            }
            continue;
        }
        if (len && !readAll(r, payload, len)) return Result::kCorrupt;
        Cur c(payload, len);
        if (std::memcmp(h, "META", 4) == 0) {
            c.str(s.name);
            c.str(s.author);
            s.bpm = c.u8();
            s.speed = c.u8();
            s.hrAD = c.u8();
            s.hrSR = c.u8();
            s.mode = c.u8() ? SongMode::kExtended : SongMode::kAuthentic;
            s.restartPos = c.u8();
            s.multi = c.left() ? c.u8() : 1;   // added in 0.8
            if (s.multi < 1 || s.multi > Song::kMaxMulti) s.multi = 1;
            if (s.bpm < 16) s.bpm = 125;
            if (!s.speed) s.speed = 6;
        } else if (std::memcmp(h, "SIDC", 4) == 0) {
            s.sid.model[0] = c.u8() ? sid::Model::k8580 : sid::Model::k6581;
            s.sid.model[1] = c.u8() ? sid::Model::k8580 : sid::Model::k6581;
            s.sid.clock = c.u8() ? SidClock::kNtsc : SidClock::kPal;
            s.sid.balance = c.u8();
            if (s.sid.balance > 100) s.sid.balance = 100;
        } else if (std::memcmp(h, "ORDR", 4) == 0) {
            const uint8_t n = c.u8();
            s.orderLen = n ? n : 1;
            for (int i = 0; i < n; ++i) {
                s.order[i].pattern = uint8_t(c.u8() & (kPatternCount - 1));
                s.order[i].transpose = int8_t(c.u8());
                const uint8_t rep = c.u8();
                s.order[i].repeat = rep ? rep : 1;
            }
        } else if (std::memcmp(h, "PATT", 4) == 0) {
            const uint8_t idx = c.u8();
            const uint8_t rows = c.u8();
            if (idx >= kPatternCount || !rows || rows > kMaxRows) return Result::kCorrupt;
            Pattern& p = s.pattern(idx);
            p.clear(rows);
            for (int row = 0; row < rows; ++row)
                for (Cell& cell : p.cells[row]) {
                    cell.note = c.u8();
                    cell.inst = c.u8();
                    cell.cmd = uint8_t(c.u8() & 15);
                    cell.param = c.u8();
                }
            if (c.left()) p.starts = uint8_t(c.u8() & 0x3F);
        } else if (std::memcmp(h, "MIXR", 4) == 0) {
            for (uint8_t& v : s.mix.volume) {
                const uint8_t x = c.left() ? c.u8() : 100;
                v = x > 100 ? 100 : x;
            }
        } else if (std::memcmp(h, "IDEF", 4) == 0) {
            initDefaults = true;
        } else if (std::memcmp(h, "INST", 4) == 0) {
            const uint8_t idx = c.u8();
            const uint8_t fields = c.u8();
            if (!idx || idx >= kInstrumentCount) return Result::kCorrupt;
            Instrument& in = s.inst[idx];
            in.clear(idx);
            uint8_t v[kInstFields];
            Instrument def;
            const uint8_t defs[kInstFields] = {Instrument::kLegacyAd, Instrument::kLegacySr, Instrument::kLegacyWave,
                                               0, 0, 0, 0, 0, def.gateTimer};
            loaded[idx] = true;
            for (int i = 0; i < kInstFields; ++i) v[i] = i < fields ? c.u8() : defs[i];
            for (int i = kInstFields; i < fields; ++i) c.u8();   // newer fields
            in.ad = v[0];
            in.sr = v[1];
            in.firstWave = v[2];
            in.wavePtr = v[3];
            in.pulsePtr = v[4];
            in.filterPtr = v[5];
            in.vibParam = v[6];
            in.vibDelay = v[7];
            in.gateTimer = v[8];
            if (c.left()) c.str(in.name);
        } else if (std::memcmp(h, "AUTO", 4) == 0) {
            const uint8_t idx = c.u8();
            c.u8();   // rows (informational)
            if (idx >= kPatternCount) return Result::kCorrupt;
            Pattern& p = s.pattern(idx);
            for (int guard = 0; guard <= kMaxRows; ++guard) {
                const uint8_t r = c.u8();
                if (r == 0xFF || c.bad()) break;
                if (r >= kMaxRows) return Result::kCorrupt;
                const uint16_t mask = c.u16();
                for (int lane = 0; lane < kAutoLanes; ++lane)
                    if ((mask >> lane) & 1) p.setAuto(r, lane, int16_t(c.u16()));
            }
        } else if (std::memcmp(h, "DRUM", 4) == 0) {
            const uint8_t idx = c.u8();
            if (idx >= kPatternCount) return Result::kCorrupt;
            Pattern& p = s.pattern(idx);
            for (int guard = 0; guard <= kMaxRows; ++guard) {
                const uint8_t r = c.u8();
                if (r == 0xFF || c.bad()) break;
                const uint8_t slot = c.u8();
                if (r < kMaxRows && slot <= 16) p.drum[r] = slot;
            }
        } else if (std::memcmp(h, "EXTN", 4) == 0) {
            const uint8_t n = c.u8();
            constexpr int kFields = 24;
            uint8_t v[kFields];
            audio::FxSettings d;
            const uint8_t defs[kFields] = {d.delayMix,   d.delayTicks, d.delayFeedback, d.chorusMix, d.chorusRate,
                                           d.chorusDepth, d.reverbMix, d.reverbSize,    d.reverbDamp, 80,
                                           0, 0, 32, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0};
            for (int i = 0; i < kFields; ++i) v[i] = i < n ? c.u8() : defs[i];
            for (int i = kFields; i < n; ++i) c.u8();
            s.drumVolume = v[9] > 100 ? 100 : v[9];
            for (int k = 0; k < 2; ++k) {
                Lfo& l = s.mod.lfo[k];
                l.target = v[10 + 4 * k] <= 8 ? v[10 + 4 * k] : 0;
                l.shape = v[11 + 4 * k] < Lfo::kShapes ? v[11 + 4 * k] : 0;
                l.period = v[12 + 4 * k] < 2 ? 2 : v[12 + 4 * k];
                l.depth = v[13 + 4 * k] > 100 ? 100 : v[13 + 4 * k];
            }
            for (int ch = 0; ch < kChannels; ++ch) {
                const int p = int8_t(v[18 + ch]);
                s.mod.pan[ch] = int8_t(p < -100 ? -100 : (p > 100 ? 100 : p));
            }
            audio::FxSettings& fx = s.fx;
            fx.delayMix = v[0] > 100 ? 100 : v[0];
            fx.delayTicks = v[1] ? (v[1] > 64 ? 64 : v[1]) : 1;
            fx.delayFeedback = v[2] > 90 ? 90 : v[2];
            fx.chorusMix = v[3] > 100 ? 100 : v[3];
            fx.chorusRate = v[4] > 100 ? 100 : v[4];
            fx.chorusDepth = v[5] > 100 ? 100 : v[5];
            fx.reverbMix = v[6] > 100 ? 100 : v[6];
            fx.reverbSize = v[7] > 100 ? 100 : v[7];
            fx.reverbDamp = v[8] > 100 ? 100 : v[8];
        } else if (std::memcmp(h, "TABL", 4) == 0) {
            const uint8_t kind = c.u8();
            const uint8_t used = c.u8();
            if (kind >= kTableKinds) continue;   // a later table kind
            Table& t = s.tables[kind];
            for (int i = 0; i < used; ++i) t.left[i] = c.u8();
            for (int i = 0; i < used; ++i) t.right[i] = c.u8();
        }
        if (c.bad()) return Result::kCorrupt;
    }
    if (!initDefaults) {
        // A file from before 0.8: a slot it left out was the silent default.
        // Where a pattern plays it, keep it silent so the song sounds as it did.
        bool used[kInstrumentCount] = {};
        for (int p = 0; p < kPatternCount; ++p) {
            const Pattern& pat = s.pattern(p);
            for (int r = 0; r < pat.rows; ++r)
                for (const Cell& cell : pat.cells[r])
                    if (cell.inst < kInstrumentCount) used[cell.inst] = true;
        }
        for (int i = 1; i < kInstrumentCount; ++i)
            if (!loaded[i] && used[i]) {
                s.inst[i].ad = Instrument::kLegacyAd;
                s.inst[i].sr = Instrument::kLegacySr;
                s.inst[i].firstWave = Instrument::kLegacyWave;
            }
    }
    if (s.restartPos >= s.orderLen) s.restartPos = 0;
    return Result::kOk;
}

}  // namespace project
}  // namespace sidt
