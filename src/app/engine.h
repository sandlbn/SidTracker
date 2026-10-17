// SIDTracker -- realtime audio engine: tracker ticks counted in audio
// samples, two SID chips, the mixer, per-section cycle profiling (PLAN.md
// §4-§6). fill() is the IAudioOut callback (DMA ISR on the device, SDL
// thread in the simulator): no allocation, no file access, no locks.
//
// Main loop -> audio: a single-producer command ring (transport, notes) and
// a versioned settings block (models, clock, balance, mutes). Song edits are
// read live by the player (single bytes; a torn read is one wrong tick).
// Structural changes (project load) bracket themselves with hold()/release().
#pragma once

#include <atomic>
#include <cstdint>

#include "audio/effects.h"
#include "audio/mixer.h"
#include "audio/pcm.h"
#include "sid/sid.h"
#include "tracker/playback/player.h"
#include "tracker/song/song.h"

namespace sidt {

// Cycle profile of the audio callback (PLAN.md §6 benchmark fields).
struct Profile {
    enum Section : uint8_t {
        kSid1Voices, kSid1Filter, kSid2Voices, kSid2Filter, kMixer, kPlayer, kPcm, kEffects, kTotal, kSections
    };
    uint32_t last[kSections] = {};
    uint32_t peak[kSections] = {};
    uint64_t sum[kSections] = {};
    uint32_t blocks = 0;
    uint32_t avg(int s) const { return blocks ? uint32_t(sum[s] / blocks) : 0; }
    static const char* name(int s);
};

// Optional observer of the register stream (tests, future export:
// docs/sid-event-stream.md). `sample` counts output samples since start.
class IStreamTap {
public:
    virtual ~IStreamTap() = default;
    virtual void onWrite(uint64_t sample, uint8_t chip, uint8_t reg, uint8_t val) = 0;
};

class Engine final : public ISidSink, public IDrumSink {
public:
    using CycleFn = uint32_t (*)();
    static constexpr uint32_t kBlock = 128;

    // sampleRateMilliHz: 44,117,647 on the device (MCLK/256), 44,100,000 host.
    void init(Song* song, uint32_t sampleRateMilliHz);
    // Delay memory for the effects (interleaved stereo int16 frames; SDRAM
    // on the device). Without it the delay is unavailable. Main loop, before
    // audio starts.
    void setFxMemory(int16_t* mem, uint32_t frames);
    // PCM drum kit memory (mono int16 samples; SDRAM on the device); builds
    // the synthesised kit. Main loop, before audio starts.
    void setKitMemory(int16_t* mem, uint32_t samples);
    // The kit, for loading WAVs: only between hold() and release().
    audio::Kit& kit() { return kit_; }
    void drumPreview(uint8_t slot) { push(kCmdDrum, slot); }
    // Global effects switch (PLAN.md §13: effects can be disabled globally).
    void setEffects(bool on);
    bool effects() const { return req_.fx; }
    uint32_t sampleRateMilliHz() const { return fs_; }
    void setCycleCounter(CycleFn fn) { cycles_ = fn; }
    void setTap(IStreamTap* tap) { tap_ = tap; }

    // IAudioOut::FillFn
    static void fillThunk(int16_t* out, uint32_t frames, void* ctx) {
        static_cast<Engine*>(ctx)->fill(out, frames);
    }
    void fill(int16_t* out, uint32_t frames);

    // --- main loop API ----------------------------------------------------
    // Lanes the song automates go back to neutral when a song starts (the
    // scan runs here, in the main loop).
    void playSong(uint8_t orderPos, uint8_t row = 0) {
        autoReset_ = song_ ? song_->automatedLanes() : 0;
        push(kCmdPlaySong, orderPos, row);
    }
    void playPattern(uint8_t pattern, uint8_t row = 0) { push(kCmdPlayPattern, pattern, row); }
    void stop() { push(kCmdStop); }
    void noteOn(int ch, uint8_t noteValue, uint8_t inst) { push(kCmdNoteOn, uint8_t(ch), noteValue, inst); }
    void noteOff(int ch) { push(kCmdNoteOff, uint8_t(ch)); }
    // Silence and reset player and chips (after a song load).
    void panic() { push(kCmdPanic); }

    // Settings, applied at the next block start.
    void setModel(int chip, sid::Model m);
    void setClock(SidClock c);
    void setBalance(uint8_t width);
    void setMasterVolume(uint8_t percent);
    void setMuted(int ch, bool on);
    void setSolo(int ch, bool on);
    bool muted(int ch) const { return (muteMask_ >> ch) & 1; }
    bool solo(int ch) const { return (soloMask_ >> ch) & 1; }
    // Copies the song's SID configuration into the settings.
    void applySongConfig();
    // Worst-case load for profiling (M2/M3 hardware tests): all six voices
    // play band-limited pulse / saw and noise through both filters with
    // high resonance; the player is paused while it is on.
    void setStress(bool on);
    bool stress() const { return stressReq_; }

    // Project load: hold() stops the callback from touching the song (it
    // outputs silence) and waits for a running callback to finish;
    // release() resumes. Bounded wait (spinLimit iterations).
    void hold();
    void release();

    // Profile window: copy and restart (main loop).
    Profile takeProfile();
    const Player& player() const { return player_; }
    Player& playerMutable() { return player_; }   // perform overrides
    const sid::Chip& chip(int i) const { return chips_[i]; }
    int16_t peak() const { return mixer_.peak(); }
    // Level meters (Master page): peaks since the last call, bus units
    // (1.0 = full scale; out is after the master volume). Main loop.
    struct Meters {
        float sid[2] = {}, drums = 0.0f, out = 0.0f;
    };
    Meters takeMeters() {
        const Meters m = meters_;
        meters_ = Meters{};
        return m;
    }
    uint32_t clips() const { return mixer_.clips(); }
    uint64_t samples() const { return samples_; }

    // ISidSink (audio context: called by the player).
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override;
    // IDrumSink (audio context: called by the player).
    void drum(uint8_t slot) override;

private:
    enum CmdType : uint8_t { kCmdPlaySong, kCmdPlayPattern, kCmdStop, kCmdNoteOn, kCmdNoteOff, kCmdPanic, kCmdDrum };
    struct Cmd {
        uint8_t type, a, b, c;
    };
    static constexpr uint32_t kRing = 64;
    void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0);
    void drain();
    void applySettings();
    void applyStress();
    uint32_t now() const { return cycles_ ? cycles_() : 0; }
    void account(int section, uint32_t c) { cur_[section] += c; }

    Song* song_ = nullptr;
    sid::Chip chips_[kChips];
    audio::Mixer mixer_;
    audio::Effects fx_;
    Meters meters_;   // written by fill(), read and cleared by takeMeters()
    audio::Kit kit_;
    audio::Pcm pcm_;
    Player player_;
    IStreamTap* tap_ = nullptr;
    CycleFn cycles_ = nullptr;
    uint32_t fs_ = 44100000;
    uint32_t toTick16_ = 0;            // samples until the next tick, 16.16
    uint16_t autoReset_ = 0;           // perform lanes to reset at song start
    int16_t* fxMem_ = nullptr;
    uint32_t fxFrames_ = 0;
    uint64_t samples_ = 0;
    float bufA_[kBlock] = {}, bufB_[kBlock] = {};
    float busL_[kBlock] = {}, busR_[kBlock] = {};
    float panBuf_[kChips][2][kBlock] = {};   // individually panned voices
    void copyPanned(int chip, uint32_t pos, uint32_t n);

    Cmd ring_[kRing];
    std::atomic<uint32_t> head_{0}, tail_{0};

    // Settings block (main loop writes, then bumps version).
    struct Settings {
        sid::Model model[kChips] = {sid::Model::k8580, sid::Model::k8580};
        SidClock clock = SidClock::kPal;
        uint8_t balance = 50;
        uint8_t volume = 80;
        uint8_t mute = 0, solo = 0;
        bool stress = false;
        bool fx = true;
    };
    Settings req_;
    Settings applied_;
    std::atomic<uint32_t> version_{0};
    uint32_t appliedVersion_ = 0;
    uint8_t muteMask_ = 0, soloMask_ = 0;
    bool stressReq_ = false;

    std::atomic<bool> hold_{false};
    std::atomic<bool> inFill_{false};

    uint32_t cur_[Profile::kSections] = {};
    Profile prof_;
};

}  // namespace sidt
