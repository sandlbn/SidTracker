// SIDTracker -- tracker playback (player.h).
//
// Playback follows GoatTracker 2's documented model (readme §3, notes in
// docs/goattracker-format.md) so imported songs play the same; details the
// documentation leaves open were settled against GoatTracker-packed songs
// (tools/oracle). Per row of `speed` ticks, per track:
//   tick speed - gatetimer: the next row is fetched; a new note there gets
//                           gate off and the hard-restart ADSR
//   tick 0:                 note init (first-frame wave), instrument and
//                           one-shot commands 5..F; tables wait a tick
//   every tick:             wave table step; when it does not set the
//                           frequency, realtime commands 1..4 or the
//                           instrument vibrato run (not on tick 0);
//                           pulse table (skipped on the fetch tick and at
//                           pattern starts); filter table per chip
// Semantics: docs/commands.md.
#include "tracker/playback/player.h"

#include <cmath>

#include "sid/regs.h"
#include "tracker/commands/commands.h"

namespace sidt {

namespace {
inline int32_t clampv(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline uint8_t nextPtr(uint8_t p) { return p >= kTableLen ? 0 : uint8_t(p + 1); }
constexpr int kJumpGuard = 8;   // table jumps followed per tick
}  // namespace

// GoatTracker 2's note table (the "default frequencytable, close to 440Hz"
// of readme §2.1), C-0..B-7. Read back from songs packed by gt2reloc
// (docs/goattracker-format.md "Frequency table"); it is the same for PAL
// and NTSC, as in GoatTracker, so NTSC plays about 3.8 % higher. A-4 =
// 0x1D46: 440.1 Hz on a PAL SID.
const uint16_t kGtFreq[kNoteCount] = {
    0x0117, 0x0127, 0x0139, 0x014B, 0x015F, 0x0174, 0x018A, 0x01A1, 0x01BA, 0x01D4, 0x01F0, 0x020E,
    0x022D, 0x024E, 0x0271, 0x0296, 0x02BE, 0x02E8, 0x0314, 0x0343, 0x0374, 0x03A9, 0x03E1, 0x041C,
    0x045A, 0x049C, 0x04E2, 0x052D, 0x057C, 0x05CF, 0x0628, 0x0685, 0x06E8, 0x0752, 0x07C1, 0x0837,
    0x08B4, 0x0939, 0x09C5, 0x0A5A, 0x0AF7, 0x0B9E, 0x0C4F, 0x0D0A, 0x0DD1, 0x0EA3, 0x0F82, 0x106E,
    0x1168, 0x1271, 0x138A, 0x14B3, 0x15EE, 0x173C, 0x189E, 0x1A15, 0x1BA2, 0x1D46, 0x1F04, 0x20DC,
    0x22D0, 0x24E2, 0x2714, 0x2967, 0x2BDD, 0x2E79, 0x313C, 0x3429, 0x3744, 0x3A8D, 0x3E08, 0x41B8,
    0x45A1, 0x49C5, 0x4E28, 0x52CD, 0x57BA, 0x5CF1, 0x6278, 0x6853, 0x6E87, 0x751A, 0x7C10, 0x8371,
    0x8B42, 0x9389, 0x9C4F, 0xA59B, 0xAF74, 0xB9E2, 0xC4F0, 0xD0A6, 0xDD0E, 0xEA33, 0xF820, 0xFFFF,
};

void FreqTable::build(uint32_t clockHz) {
    clockHz_ = clockHz;
    for (int s = 0; s < kNoteCount; ++s) f_[s] = kGtFreq[s];
}

void Player::init(const Song* song, ISidSink* sink, IDrumSink* drums) {
    song_ = song;
    sink_ = sink;
    drums_ = drums;
    freq_.build(song ? song->sid.clockHz() : sid::kPalClockHz);
    reset();
}

void Player::reset() {
    for (Chan& c : ch_) c = Chan{};
    for (ChipState& s : chip_) s = ChipState{};
    mode_ = Mode::kStopped;
    tick_ = row_ = 0;
    funkRow_ = 0;
}

void Player::stop() {
    mode_ = Mode::kStopped;
    tick_ = 0;
    for (Chan& c : ch_) {
        c.gateMask = 0xFE;
        c.wavePtr = c.pulsePtr = 0;
        c.pulseTicks = c.waveDelay = 0;
        c.cmd = c.param = 0;
        c.tonePorta = false;
        c.fetched = false;
    }
    for (ChipState& s : chip_) s.filterPtr = s.filterTicks = 0;
}

void Player::loadOrder(uint8_t pos) {
    const OrderEntry& e = song_->order[pos];
    pattern_ = uint8_t(e.pattern & (kPatternCount - 1));
    transpose_ = e.transpose;
    repeatLeft_ = e.repeat ? e.repeat : 1;
}

void Player::playSong(uint8_t orderPos, uint8_t row) {
    if (!song_) return;
    orderPos_ = orderPos < song_->orderLen ? orderPos : 0;
    loadOrder(orderPos_);
    row_ = row < song_->pattern(pattern_).rows ? row : 0;
    tick_ = 0;
    speed_ = song_->speed ? song_->speed : 6;
    funkRow_ = 0;
    firstRow_ = true;
    patternStart_ = true;
    for (Chan& c : ch_) {
        c.cmd = c.param = 0;
        c.fetched = false;
    }
    lfoTick_[0] = lfoTick_[1] = 0;
    mode_ = Mode::kSong;
}

void Player::playPattern(uint8_t pattern, uint8_t row) {
    if (!song_) return;
    pattern_ = uint8_t(pattern & (kPatternCount - 1));
    transpose_ = 0;
    repeatLeft_ = 1;
    row_ = row < song_->pattern(pattern_).rows ? row : 0;
    tick_ = 0;
    speed_ = song_->speed ? song_->speed : 6;
    funkRow_ = 0;
    firstRow_ = true;
    patternStart_ = true;
    for (Chan& c : ch_) {
        c.cmd = c.param = 0;
        c.fetched = false;
    }
    mode_ = Mode::kPattern;
}

int16_t Player::performLane(int lane) const {
    if (lane < kAutoPw) return perform.wave[lane - kAutoWave];
    if (lane < kAutoCutoff) return perform.pwOffset[lane - kAutoPw];
    if (lane < kAutoRes) return perform.cutoffOffset[lane - kAutoCutoff];
    return perform.resonance[(lane - kAutoRes) & 1];
}

void Player::setPerformLane(int lane, int16_t v) {
    if (lane < 0 || lane >= kAutoLanes) return;
    if (lane < kAutoPw) perform.wave[lane - kAutoWave] = uint8_t(v & 0xF0);
    else if (lane < kAutoCutoff) perform.pwOffset[lane - kAutoPw] = int16_t(clampv(v, -0xFFF, 0xFFF));
    else if (lane < kAutoRes) perform.cutoffOffset[lane - kAutoCutoff] = int16_t(clampv(v, -2047, 2047));
    else perform.resonance[lane - kAutoRes] = int8_t(clampv(v, -1, 15));
}

void Player::resetPerformLanes(uint16_t mask) {
    for (int lane = 0; lane < kAutoLanes; ++lane)
        if ((mask >> lane) & 1) setPerformLane(lane, lane >= kAutoRes ? -1 : 0);
}

void Player::noteOn(int ch, uint8_t noteValue, uint8_t inst) {
    if (!song_ || ch < 0 || ch >= kChannels || !note::isNote(noteValue)) return;
    Chan& c = ch_[ch];
    c.inst = inst < kInstrumentCount ? inst : 0;
    c.cmd = c.param = 0;
    // Live notes have no fetch tick: gate off now so the attack restarts.
    c.gateMask = 0xFE;
    writeChannel(ch);
    initNote(ch, uint8_t(noteValue - 1));
}

void Player::noteOff(int ch) {
    if (ch >= 0 && ch < kChannels) ch_[ch].gateMask = 0xFE;
}

uint32_t Player::samplesPerTick16(uint32_t sampleRateMilliHz) const {
    const uint32_t bpm = song_ && song_->bpm >= 16 ? song_->bpm : 125;
    const uint32_t multi = song_ ? song_->multiSpeed() : 1;
    return uint32_t(uint64_t(sampleRateMilliHz) * 5u * 65536u / (2000u * bpm * multi));
}

uint16_t Player::speedEntry(uint8_t row) const {
    const Table& t = song_->tables[kSpeedTable];
    return uint16_t((t.l(row) << 8) | t.r(row));
}

uint16_t Player::speedFor(const Chan& c, uint8_t row, bool vibrato) const {
    const Table& t = song_->tables[kSpeedTable];
    const uint8_t L = t.l(row), R = t.r(row);
    if (!(L & 0x80)) return vibrato ? R : uint16_t((L << 8) | R);
    // Note-independent: a fraction of the note's semitone step. UNVERIFIED
    // formula (docs/goattracker-format.md unknowns): step >> R.
    const int n = c.note;
    const int step = int(freq_.freq(n + 1)) - int(freq_.freq(n));
    return uint16_t(step > 0 ? (step >> (R & 15)) : 0);
}

const Cell& Player::cellAt(uint8_t pattern, uint8_t row, int ch) const {
    return song_->pattern(pattern).cells[row & (kMaxRows - 1)][ch];
}

void Player::peekNext(uint8_t& pattern, uint8_t& row, int8_t& transpose, bool& newPattern) const {
    const Pattern& p = song_->pattern(pattern_);
    newPattern = false;
    if (row_ + 1 < p.rows) {
        pattern = pattern_;
        row = uint8_t(row_ + 1);
        transpose = transpose_;
        return;
    }
    row = 0;
    newPattern = true;
    if (mode_ == Mode::kPattern || repeatLeft_ > 1) {
        pattern = pattern_;
        transpose = transpose_;
        return;
    }
    uint8_t next = uint8_t(orderPos_ + 1);
    if (next >= song_->orderLen) next = song_->restartPos < song_->orderLen ? song_->restartPos : 0;
    pattern = uint8_t(song_->order[next].pattern & (kPatternCount - 1));
    transpose = song_->order[next].transpose;
}

uint8_t Player::fetchTick(const Chan& c) const {
    const uint8_t t = uint8_t(song_->inst[c.inst].gateTimer & 0x3F);
    return t && t < speed_ ? uint8_t(speed_ - t) : 0;
}

void Player::fetch(int ch, bool hardRestart) {
    Chan& c = ch_[ch];
    uint8_t p, r;
    int8_t tr;
    bool np;
    if (firstRow_ || !hardRestart) {
        // Playback start, or a fetch on tick 0 (no early fetch tick): the
        // current row.
        p = pattern_;
        r = row_;
        tr = transpose_;
        np = true;
    } else {
        peekNext(p, r, tr, np);
    }
    c.next = cellAt(p, r, ch);
    c.nextTranspose = tr;
    c.newPattern = np;
    c.fetched = true;
    // A key off takes effect when its row is fetched (measured).
    if (c.next.note == note::kKeyOff) c.gateMask = 0xFE;
    if (!hardRestart || !note::isNote(c.next.note) || (c.next.cmd & 15) == cmd::kTonePorta) return;
    // The new note's instrument decides about gate off / hard restart.
    const uint8_t inst = c.next.inst && c.next.inst < kInstrumentCount ? c.next.inst : c.inst;
    const uint8_t gt = song_->inst[inst].gateTimer;
    if (gt & 0x40) return;   // legato: neither gate off nor hard restart
    c.gateMask = 0xFE;
    if (!(gt & 0x80)) {
        c.ad = song_->hrAD;
        c.sr = song_->hrSR;
    }
}

void Player::tick() {
    if (!song_ || !sink_) return;
    bool rowStart = false;
    bool advance[kChannels] = {};
    lfoStep();
    // Filter tables and filter/volume registers first: a filter pointer or
    // B/C/D command from this tick's rows takes effect on the next tick (as
    // in GoatTracker, measured).
    for (int chip = 0; chip < kChips; ++chip) {
        filterStep(chip_[chip]);
        writeChip(chip);
    }
    if (playing()) {
        if (tick_ == 0) {
            rowStart = true;
            if (funkRow_) {
                const uint16_t e = speedEntry(funkRow_);
                const uint8_t s = funkPhase_ ? uint8_t(e & 0xFF) : uint8_t(e >> 8);
                speed_ = s ? s : 1;
                funkPhase_ = !funkPhase_;
            }
            for (int ch = 0; ch < kChannels; ++ch) {
                if (!ch_[ch].fetched) fetch(ch, false);
                applyRow(ch);
            }
            firstRow_ = false;
            // Perform automation (M7) and the drum column (M8) of this row.
            const Pattern& ap = song_->pattern(pattern_);
            if (drums_ && row_ < ap.rows && ap.drum[row_]) drums_->drum(ap.drum[row_]);
            if (row_ < ap.rows && ap.autoSet[row_]) {
                for (int lane = 0; lane < kAutoLanes; ++lane)
                    if ((ap.autoSet[row_] >> lane) & 1) setPerformLane(lane, ap.autoVal[row_][lane]);
            }
            // GoatTracker advances a channel's order list on tick 0 of the
            // pattern's last row: no pulse step there (readme §3.5, -O).
            uint8_t np, nr;
            int8_t ntr;
            bool newPat;
            peekNext(np, nr, ntr, newPat);
            for (int ch = 0; ch < kChannels; ++ch)
                advance[ch] = newPat && ((song_->pattern(np).starts >> ch) & 1);
        } else {
            for (int ch = 0; ch < kChannels; ++ch)
                if (!ch_[ch].fetched && tick_ == fetchTick(ch_[ch])) fetch(ch, true);
        }
    }
    for (int ch = 0; ch < kChannels; ++ch) {
        Chan& c = ch_[ch];
        bool freqSet = false;
        const bool initTick = c.init;   // note init: wave and pulse tables start next tick
        if (initTick) c.init = false;
        else freqSet = waveStep(ch);
        if (!initTick && !(realtimeSkip && rowStart)) realtime(c, freqSet);
        const bool skipPulse = initTick || (pulseSkip && playing() &&
                                            ((tick_ != 0 && tick_ == fetchTick(c)) || (rowStart && advance[ch])));
        if (!skipPulse) pulseStep(c);
        writeChannel(ch);
        if (c.sinceNote < 255) ++c.sinceNote;
    }
    if (playing() && ++tick_ >= speed_) {
        tick_ = 0;
        nextRow();
    }
    ++ticks_;
}

void Player::lfoStep() {
    for (int16_t& v : lfoPw_) v = 0;
    lfoCut_[0] = lfoCut_[1] = 0;
    for (int k = 0; k < 2; ++k) {
        const Lfo& l = song_->mod.lfo[k];
        if (!l.active()) continue;
        // The period counts frames: multispeed does not speed the LFOs up.
        const uint32_t period = (l.period < 2 ? 2u : uint32_t(l.period)) * song_->multiSpeed();
        const uint32_t ph = lfoTick_[k] % period;
        const float x = float(ph) / float(period);
        float v;
        switch (l.shape) {
            case Lfo::kSine: v = std::sin(6.2831853f * x); break;
            case Lfo::kSquare: v = x < 0.5f ? 1.0f : -1.0f; break;
            case Lfo::kSaw: v = 2.0f * x - 1.0f; break;
            case Lfo::kRandom:
                if (ph == 0) {
                    lfoRand_ ^= lfoRand_ << 13;
                    lfoRand_ ^= lfoRand_ >> 17;
                    lfoRand_ ^= lfoRand_ << 5;
                    lfoHold_[k] = int16_t(int32_t(lfoRand_ & 0xFFFF) - 0x8000);
                }
                v = float(lfoHold_[k]) / 32768.0f;
                break;
            default: v = x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x; break;
        }
        const float a = v * float(l.depth) / 100.0f;
        if (l.target >= 1 && l.target <= 6) lfoPw_[l.target - 1] = int16_t(lfoPw_[l.target - 1] + a * 2000.0f);
        else if (l.target >= 7 && l.target <= 8) lfoCut_[l.target - 7] = int16_t(lfoCut_[l.target - 7] + a * 1000.0f);
        ++lfoTick_[k];
    }
}

void Player::nextRow() {
    patternStart_ = false;
    if (++row_ < song_->pattern(pattern_).rows) return;
    row_ = 0;
    patternStart_ = true;
    if (mode_ == Mode::kPattern) return;
    if (repeatLeft_ > 1) {
        --repeatLeft_;
        return;
    }
    uint8_t next = uint8_t(orderPos_ + 1);
    if (next >= song_->orderLen) next = song_->restartPos < song_->orderLen ? song_->restartPos : 0;
    orderPos_ = next;
    loadOrder(next);
}

void Player::applyRow(int ch) {
    Chan& c = ch_[ch];
    const Cell cell = c.next;
    c.fetched = false;
    const uint8_t cm = cell.cmd & 15;
    if (cell.inst && cell.inst < kInstrumentCount) c.inst = cell.inst;
    // Realtime commands 1..4 replace the previous one; 0 ends it. One-shot
    // commands leave the running realtime command alone (readme §3.2).
    if (cm <= cmd::kVibrato) {
        c.cmd = cm;
        c.param = cell.param;
        if (cm != cmd::kTonePorta) c.tonePorta = false;
    }
    if (note::isNote(cell.note)) {
        const uint8_t semi = uint8_t(clampv(int(cell.note) - 1 + c.nextTranspose, 0, kNoteCount - 1));
        if (cm == cmd::kTonePorta) {
            // Tone portamento towards the note, no retrigger; 3 00 ties: the
            // pitch jumps on the first realtime tick (tick 1, measured).
            c.note = semi;
            c.tpTarget = freq_.freq(semi);
            c.tonePorta = true;
        } else {
            initNote(ch, semi);
        }
    } else if (cell.note == note::kKeyOff) {
        c.gateMask = 0xFE;
    } else if (cell.note == note::kKeyOn) {
        c.gateMask = 0xFF;
    }
    if (cm >= cmd::kSetAD) oneShot(ch, cm, cell.param);
}

void Player::initNote(int ch, uint8_t semi) {
    Chan& c = ch_[ch];
    const Instrument& in = song_->inst[c.inst];
    c.note = semi;
    c.ad = in.ad;
    c.sr = in.sr;
    c.tonePorta = false;
    c.vibPhase = 0;
    c.rtTicks = 0;
    c.sinceNote = 0;
    c.wavePtr = in.wavePtr;
    c.waveDelay = 0;
    if (in.pulsePtr) {
        c.pulsePtr = in.pulsePtr;
        c.pulseTicks = 0;
    }
    if (in.filterPtr) {
        chip_[ch / 3].filterPtr = in.filterPtr;
        chip_[ch / 3].filterTicks = 0;
    }
    // First-frame wave: 01..FD written (gate from its bit 0 through the
    // mask), FE gate off / FF gate on with the waveform unchanged, 00 both
    // unchanged (readme §3.3, v2.59 notes).
    const uint8_t fw = in.firstWave;
    if (fw == 0xFE) {
        c.gateMask = 0xFE;
    } else if (fw == 0xFF) {
        c.gateMask = 0xFF;
    } else if (fw) {
        c.wave = fw;
        c.gateMask = 0xFF;
    }
    c.init = true;
}

void Player::oneShot(int ch, uint8_t cm, uint8_t p) {
    Chan& c = ch_[ch];
    ChipState& s = chip_[ch / 3];
    switch (cm) {
        case cmd::kSetAD: c.ad = p; break;
        case cmd::kSetSR: c.sr = p; break;
        case cmd::kSetWave: c.wave = p; break;
        case cmd::kWavePtr:
            c.wavePtr = p;
            c.waveDelay = 0;
            break;
        case cmd::kPulsePtr:
            c.pulsePtr = p;
            c.pulseTicks = 0;
            break;
        case cmd::kFilterPtr:
            s.filterPtr = p;
            s.filterTicks = 0;
            break;
        case cmd::kFilterCtrl:
            s.resRoute = p;
            if (!p) s.filterPtr = s.filterTicks = 0;   // B00: filter off, table stopped
            break;
        case cmd::kCutoff: s.cutoff = p; break;
        case cmd::kVolume:
            if (!(p & 0xF0)) s.volume = uint8_t(p & 15);   // X != 0: timing mark, no sound effect
            break;
        case cmd::kFunkTempo:
            if (p) {
                funkRow_ = lastFunkRow_ = p;
                const uint8_t first = uint8_t(speedEntry(p) >> 8);
                speed_ = first ? first : 1;
                funkPhase_ = true;
            }
            break;
        case cmd::kTempo: {
            const uint8_t t = uint8_t(p & 0x7F);   // 83..FF: per-channel in GT, global here
            if (t >= 2) {
                speed_ = t;
                funkRow_ = 0;
            } else if (lastFunkRow_) {   // 00/01: recall the funk tempo
                funkRow_ = lastFunkRow_;
            }
            break;
        }
        default: break;
    }
}

bool Player::waveStep(int ch) {
    Chan& c = ch_[ch];
    if (!c.wavePtr) return false;
    const Table& t = song_->tables[kWaveTable];
    // Delay row 01..0F: L ticks of waiting, then the row's note on the
    // next tick (measured: the step takes L + 1 ticks).
    if (c.waveDelay) {
        if (--c.waveDelay) return false;
        const uint8_t R = t.r(c.wavePtr);
        c.wavePtr = nextPtr(c.wavePtr);
        if (R == 0x80) return false;
        int semi;
        if (R < 0x60) semi = c.note + R;
        else if (R < 0x80) semi = c.note + int(R) - 0x80;
        else semi = R - 0x80;
        c.freq = freq_.freq(semi);
        return true;
    }
    for (int guard = 0; guard < kJumpGuard; ++guard) {
        const uint8_t L = t.l(c.wavePtr), R = t.r(c.wavePtr);
        if (L == 0xFF) {
            if (!R) {
                c.wavePtr = 0;
                return false;
            }
            c.wavePtr = R;
            continue;
        }
        if (L >= 0x01 && L <= 0x0F) {
            c.waveDelay = L;   // the pointer stays on this row until the delay ends
            return false;
        }
        c.wavePtr = nextPtr(c.wavePtr);
        if (L >= 0xF0) {   // pattern command from the table; no wave or note change
            const uint8_t cm = uint8_t(L & 0x0F);
            if (cm >= cmd::kSetAD) {
                oneShot(ch, cm, R);
            } else if (cm >= cmd::kPortaUp) {
                // 1XY..4XY from the table act once, on this tick; the row's
                // own realtime command is left alone (measured).
                effect(c, cm, R);
            }
            // No other realtime effect on this tick (measured).
            return true;
        }
        if (L >= 0x10 && L <= 0xDF) c.wave = L;
        else if (L >= 0xE0) c.wave = uint8_t(L & 0x0F);
        if (R == 0x80) return false;
        int semi;
        if (R < 0x60) semi = c.note + R;
        else if (R < 0x80) semi = c.note + int(R) - 0x80;
        else semi = R - 0x80;   // 81..DF: absolute C#0..B-7; E0..FF undefined in GT (clamped to B-7)
        c.freq = freq_.freq(semi);
        return true;
    }
    c.wavePtr = 0;
    return false;
}

bool Player::effect(Chan& c, uint8_t cm, uint8_t param) {
    switch (cm) {
        case cmd::kPortaUp: c.freq = uint16_t(c.freq + speedFor(c, param, false)); return true;
        case cmd::kPortaDown: c.freq = uint16_t(c.freq - speedFor(c, param, false)); return true;
        case cmd::kTonePorta: {
            if (!c.tonePorta) return true;
            const uint16_t sp = param ? speedFor(c, param, false) : 0xFFFF;
            if (c.freq < c.tpTarget) c.freq = uint16_t(c.tpTarget - c.freq <= sp ? c.tpTarget : c.freq + sp);
            else c.freq = uint16_t(c.freq - c.tpTarget <= sp ? c.tpTarget : c.freq - sp);
            if (c.freq == c.tpTarget) c.tonePorta = false;
            return true;
        }
        case cmd::kVibrato: {
            // Measured on GT-packed songs (docs/goattracker-format.md): the
            // direction changes every speed + 2 pitch steps, the first half
            // cycle (up) is half as long.
            const uint32_t spd = song_->tables[kSpeedTable].l(param) & 0x7F;
            const uint32_t half = spd + 2, first = half / 2;
            const uint32_t ph = c.vibPhase;
            const bool up = ph < first || (((ph - first) / half) & 1);
            const uint16_t depth = speedFor(c, param, true);
            c.freq = uint16_t(up ? c.freq + depth : c.freq - depth);
            if (c.vibPhase < 0xFFFF) ++c.vibPhase;
            return true;
        }
        default: return false;
    }
}

void Player::realtime(Chan& c, bool freqSet) {
    if (freqSet) return;
    if (c.cmd >= cmd::kPortaUp && c.cmd <= cmd::kTonePorta) {
        effect(c, c.cmd, c.param);
        c.rtTicks = 0;   // a running 1..3 restarts the instrument vibrato (measured)
        c.vibPhase = 0;
        return;
    }
    // Vibrato: 4XY, else the instrument's from the `vibDelay`-th realtime
    // tick on which the wave table left the pitch alone (measured).
    if (c.cmd == cmd::kVibrato) {
        if (c.param) effect(c, cmd::kVibrato, c.param);
        return;
    }
    if (c.rtTicks < 255) ++c.rtTicks;
    const Instrument& in = song_->inst[c.inst];
    if (in.vibDelay && in.vibParam && c.rtTicks >= in.vibDelay) effect(c, cmd::kVibrato, in.vibParam);
}

void Player::pulseStep(Chan& c) {
    if (c.pulseTicks) {
        c.pw = uint16_t((c.pw + c.pulseSpeed) & 0xFFF);
        --c.pulseTicks;
        return;
    }
    if (!c.pulsePtr) return;
    const Table& t = song_->tables[kPulseTable];
    for (int guard = 0; guard < kJumpGuard; ++guard) {
        const uint8_t L = t.l(c.pulsePtr), R = t.r(c.pulsePtr);
        if (L == 0xFF) {
            if (!R) {
                c.pulsePtr = 0;
                return;
            }
            c.pulsePtr = R;
            continue;
        }
        c.pulsePtr = nextPtr(c.pulsePtr);
        if (L >= 0x80) {
            c.pw = uint16_t(((L & 0x0F) << 8) | R);
        } else {
            // 01..7F ticks; 00 = 256 ticks (GoatTracker's counter wraps;
            // undocumented, seen in GT-packed songs).
            c.pulseSpeed = int8_t(R);
            c.pulseTicks = uint16_t((L ? L : 256) - 1);
            c.pw = uint16_t((c.pw + c.pulseSpeed) & 0xFFF);
        }
        return;
    }
    c.pulsePtr = 0;
}

void Player::filterStep(ChipState& s) {
    if (s.filterTicks) {
        s.cutoff = uint8_t(s.cutoff + s.filterSpeed);
        --s.filterTicks;
        return;
    }
    if (!s.filterPtr) return;
    const Table& t = song_->tables[kFilterTable];
    for (int guard = 0; guard < kJumpGuard; ++guard) {
        const uint8_t L = t.l(s.filterPtr), R = t.r(s.filterPtr);
        if (L == 0xFF) {
            if (!R) {
                s.filterPtr = 0;
                return;
            }
            s.filterPtr = R;
            continue;
        }
        s.filterPtr = nextPtr(s.filterPtr);
        if (L >= 0x80) {
            s.mode = uint8_t(L & 0x70);
            s.resRoute = R;
            // "Set filter parameters" followed by "set cutoff": same tick.
            if (t.l(s.filterPtr) == 0x00 && s.filterPtr) {
                s.cutoff = t.r(s.filterPtr);
                s.filterPtr = nextPtr(s.filterPtr);
            }
        } else if (L == 0) {
            s.cutoff = R;
        } else {
            s.filterSpeed = int8_t(R);
            s.filterTicks = uint8_t(L - 1);
            s.cutoff = uint8_t(s.cutoff + s.filterSpeed);
        }
        return;
    }
    s.filterPtr = 0;
}

void Player::rewriteChip(int chip) {
    if (chip < 0 || chip >= 2) return;
    for (int v = 0; v < 3; ++v)
        for (int16_t& sh : ch_[chip * 3 + v].sh) sh = -1;
    for (int16_t& sh : chip_[chip].sh) sh = -1;
}

void Player::put(int16_t& shadow, uint8_t chip, uint8_t reg, uint8_t val) {
    if (shadow == val) return;
    shadow = val;
    sink_->sidWrite(chip, reg, val);
}

void Player::writeChannel(int ch) {
    Chan& c = ch_[ch];
    const uint8_t chip = uint8_t(ch / 3);
    const uint8_t base = uint8_t((ch % 3) * sid::kVoiceStride);
    const uint8_t pwave = perform.wave[ch];
    const uint8_t wave = pwave ? uint8_t((pwave & 0xF0) | (c.wave & 0x0F)) : c.wave;
    const uint16_t pw = uint16_t(clampv(int(c.pw) + perform.pwOffset[ch] + lfoPw_[ch], 0, 0xFFF));
    put(c.sh[sid::kAD], chip, uint8_t(base + sid::kAD), c.ad);
    put(c.sh[sid::kSR], chip, uint8_t(base + sid::kSR), c.sr);
    put(c.sh[sid::kFreqLo], chip, uint8_t(base + sid::kFreqLo), uint8_t(c.freq & 0xFF));
    put(c.sh[sid::kFreqHi], chip, uint8_t(base + sid::kFreqHi), uint8_t(c.freq >> 8));
    put(c.sh[sid::kPwLo], chip, uint8_t(base + sid::kPwLo), uint8_t(pw & 0xFF));
    put(c.sh[sid::kPwHi], chip, uint8_t(base + sid::kPwHi), uint8_t(pw >> 8));
    put(c.sh[sid::kControl], chip, uint8_t(base + sid::kControl), uint8_t(wave & c.gateMask));
}

void Player::writeChip(int chip) {
    ChipState& s = chip_[chip];
    const int fc = clampv(int(s.cutoff) * 8 + perform.cutoffOffset[chip] + lfoCut_[chip], 0, 2047);
    const int8_t pres = perform.resonance[chip];
    const uint8_t res = pres >= 0 ? uint8_t(pres & 15) : uint8_t(s.resRoute >> 4);
    put(s.sh[0], uint8_t(chip), sid::kFcLo, uint8_t(fc & 7));
    put(s.sh[1], uint8_t(chip), sid::kFcHi, uint8_t(fc >> 3));
    put(s.sh[2], uint8_t(chip), sid::kResFilt, uint8_t((res << 4) | (s.resRoute & 0x0F)));
    put(s.sh[3], uint8_t(chip), sid::kModeVol, uint8_t(s.mode | s.volume));
}

}  // namespace sidt
