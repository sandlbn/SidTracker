// SIDTracker -- tracker playback (PLAN.md §4 "Tracker Engine", §5 timing).
//
// Runs once per tracker tick (counted in audio samples by the caller, never
// UI frames), reads rows, executes instruments' wave / pulse / filter tables
// and pattern commands, and emits SID register writes into an ISidSink: the
// "Event / Register Stream" layer. Only registers whose value changed are
// written, so the stream is also a compact export format.
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {

class ISidSink {
public:
    virtual ~ISidSink() = default;
    virtual void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) = 0;
};

// PCM drum triggers from the drum column (M8).
class IDrumSink {
public:
    virtual ~IDrumSink() = default;
    virtual void drum(uint8_t slot) = 0;   // kit slot 1..16
};

// Semitone (0 = C-0 .. 95 = B-7) -> 16-bit SID frequency: GoatTracker's
// table, independent of the SID clock (player.cpp).
class FreqTable {
public:
    void build(uint32_t clockHz);
    uint16_t freq(int semitone) const {   // 0 = C-0 .. 95 = B-7 (clamped)
        return f_[semitone < 0 ? 0 : (semitone >= kNoteCount ? kNoteCount - 1 : semitone)];
    }
    uint32_t clockHz() const { return clockHz_; }

private:
    uint16_t f_[kNoteCount] = {};
    uint32_t clockHz_ = 0;
};

// Live Perform-page overrides applied on top of the song (PLAN.md §10).
struct PerformOverrides {
    uint8_t wave[kChannels] = {};       // 0 = song; else waveform bits (0x10..0x80)
    int16_t pwOffset[kChannels] = {};   // added to the pulse width (clamped 0..0xFFF)
    int16_t cutoffOffset[kChips] = {};  // added to the 11-bit cutoff
    int8_t resonance[kChips] = {-1, -1};// -1 = song, else 0..15
};

class Player {
public:
    enum class Mode : uint8_t { kStopped, kSong, kPattern };

    void init(const Song* song, ISidSink* sink, IDrumSink* drums = nullptr);
    void setSong(const Song* song) { song_ = song; }
    // Rebuilds the note table for a SID clock (main loop or audio context:
    // pure arithmetic, no allocation).
    void setClock(uint32_t clockHz) { freq_.build(clockHz); }

    void playSong(uint8_t orderPos, uint8_t row = 0);
    void playPattern(uint8_t pattern, uint8_t row = 0);
    // Stop: gates off (notes release), tables stop, registers kept.
    void stop();
    // Silence immediately and forget all channel state (song load, reset).
    void reset();

    // Perform automation: lanes in `mask` go back to neutral (playback start
    // of a song that automates them).
    void resetPerformLanes(uint16_t mask);
    // Value of a lane in `perform` (for recording) and setting it.
    int16_t performLane(int lane) const;
    void setPerformLane(int lane, int16_t v);

    // Preview / live play on a channel (pads), works stopped or playing.
    void noteOn(int ch, uint8_t noteValue, uint8_t inst);
    void noteOff(int ch);

    // One tracker tick.
    void tick();
    // Forgets what was written to `chip`: the next tick writes every one of
    // its registers again (the chip was reset or used for something else).
    void rewriteChip(int chip);
    // Audio samples per tick for the song tempo at `sampleRateMilliHz`,
    // 16.16 fixed point (bpm * 2 / 5 * multi ticks per second).
    uint32_t samplesPerTick16(uint32_t sampleRateMilliHz) const;

    // Playback position (read by the UI; single words, torn reads harmless).
    Mode mode() const { return mode_; }
    bool playing() const { return mode_ != Mode::kStopped; }
    uint8_t orderPos() const { return orderPos_; }
    uint8_t patternIndex() const { return pattern_; }
    uint8_t row() const { return row_; }
    uint8_t tickInRow() const { return tick_; }
    uint8_t speed() const { return speed_; }
    uint32_t ticks() const { return ticks_; }

    // Per-channel state for meters and the Perform page.
    uint8_t channelNote(int ch) const { return ch_[ch].note; }
    uint8_t channelInst(int ch) const { return ch_[ch].inst; }
    bool channelGate(int ch) const { return (ch_[ch].wave & ch_[ch].gateMask & 1) != 0; }
    uint8_t channelWave(int ch) const { return ch_[ch].wave; }
    uint16_t channelPw(int ch) const { return ch_[ch].pw; }
    uint16_t channelFreq(int ch) const { return ch_[ch].freq; }
    uint16_t chipCutoff(int chip) const { return uint16_t(chip_[chip].cutoff << 3); }
    uint8_t chipResonance(int chip) const { return uint8_t(chip_[chip].resRoute >> 4); }

    PerformOverrides perform;
    // GoatTracker playback options (gt2reloc -O / -R): skip the pulse table
    // on the row-fetch tick and at pattern starts; skip realtime commands
    // and instrument vibrato on tick 0. On by default, as in GoatTracker.
    bool pulseSkip = true;
    bool realtimeSkip = true;

private:
    struct Chan {
        uint8_t note = 0;          // semitone 0..95 (after transpose)
        uint8_t inst = 0;
        uint8_t gateMask = 0xFE;   // 0xFF gate on, 0xFE gate off (ANDed with wave)
        uint8_t wave = 0;          // control value from the tables (gate bit included)
        uint8_t ad = 0, sr = 0;
        uint16_t pw = 0;
        uint16_t freq = 0;         // current frequency register value
        uint8_t wavePtr = 0, waveDelay = 0;
        uint8_t pulsePtr = 0;
        uint16_t pulseTicks = 0;
        int8_t pulseSpeed = 0;
        uint8_t cmd = 0, param = 0;   // realtime command of the row (1..4)
        uint16_t tpTarget = 0;
        bool tonePorta = false;
        uint16_t vibPhase = 0;     // vibrato pitch steps since the note init
        uint8_t rtTicks = 0;       // realtime ticks without a wave-table pitch since the init (vibrato delay)
        uint8_t sinceNote = 0;
        bool init = false;         // note initialised this tick: tables start next tick
        bool fetched = false;      // next row already read (fetch tick passed)
        Cell next;                 // the fetched row
        int8_t nextTranspose = 0;
        bool newPattern = false;   // this row starts a pattern (pulse skip)
        // Shadow of the written registers (-1 = force).
        int16_t sh[7] = {-1, -1, -1, -1, -1, -1, -1};
    };
    struct ChipState {
        uint8_t filterPtr = 0, filterTicks = 0;
        int8_t filterSpeed = 0;
        uint8_t cutoff = 0;        // high 8 bits
        uint8_t resRoute = 0;      // RES/FILT register
        uint8_t mode = 0;          // LP/BP/HP bits
        uint8_t volume = 15;
        int16_t sh[4] = {-1, -1, -1, -1};
    };

    const Cell& cellAt(uint8_t pattern, uint8_t row, int ch) const;
    void loadOrder(uint8_t pos);
    uint8_t fetchTick(const Chan& c) const;
    void fetch(int ch, bool hardRestart);
    void applyRow(int ch);
    void initNote(int ch, uint8_t semitone);
    void oneShot(int ch, uint8_t cmd, uint8_t param);
    bool waveStep(int ch);          // true when it set the frequency or ran a command
    // Realtime commands 1..4 and instrument vibrato on a realtime tick (not
    // the note's init tick, not a skipped tick 0); `freqSet`: the wave table
    // set the pitch this tick, so it does not move.
    void realtime(Chan& c, bool freqSet);
    // One step of effect 1..4 with `param` (also used by wave table F1..F4).
    bool effect(Chan& c, uint8_t cmd, uint8_t param);
    void pulseStep(Chan& c);
    void filterStep(ChipState& s);
    void writeChannel(int ch);
    void writeChip(int chip);
    void put(int16_t& shadow, uint8_t chip, uint8_t reg, uint8_t val);
    void nextRow();
    uint16_t speedEntry(uint8_t row) const;
    // Speed for portamento / vibrato depth from a speed table row, with the
    // note-independent mode (left byte bit 7: right byte = right shift of
    // the note's semitone step).
    uint16_t speedFor(const Chan& c, uint8_t row, bool vibrato) const;
    // Pattern, row and transpose of the row after the current one.
    void peekNext(uint8_t& pattern, uint8_t& row, int8_t& transpose, bool& newPattern) const;

    void lfoStep();
    int16_t lfoPw_[kChannels] = {};   // LFO offsets this tick (M8)
    int16_t lfoCut_[kChips] = {};
    uint32_t lfoTick_[2] = {};
    int16_t lfoHold_[2] = {};
    uint32_t lfoRand_ = 0x2468ACE1u;

    const Song* song_ = nullptr;
    ISidSink* sink_ = nullptr;
    IDrumSink* drums_ = nullptr;
    FreqTable freq_;
    Chan ch_[kChannels];
    ChipState chip_[kChips];
    Mode mode_ = Mode::kStopped;
    uint8_t orderPos_ = 0, pattern_ = 0, row_ = 0, tick_ = 0;
    uint8_t repeatLeft_ = 1;
    int8_t transpose_ = 0;
    uint8_t speed_ = 6;
    uint8_t funkRow_ = 0;          // speed table row of an active funk tempo (0 = off)
    uint8_t lastFunkRow_ = 0;      // for F00 / F01
    bool funkPhase_ = false;
    bool firstRow_ = false;        // playback just started: row_ is current, not advanced
    bool patternStart_ = false;    // the current row starts a pattern
    uint32_t ticks_ = 0;
};

}  // namespace sidt
