// SIDTracker -- realtime audio engine (engine.h).
#include "app/engine.h"

#include <cstring>

namespace sidt {

const char* Profile::name(int s) {
    static const char* const kNames[kSections] = {"SID1 voices", "SID1 filter", "SID2 voices", "SID2 filter",
                                                  "mixer",       "player",      "PCM",         "effects",
                                                  "total"};
    return s >= 0 && s < kSections ? kNames[s] : "?";
}

void Engine::init(Song* song, uint32_t sampleRateMilliHz) {
    song_ = song;
    fs_ = sampleRateMilliHz ? sampleRateMilliHz : 44100000;
    if (!sid::tablesReady()) sid::initTables(fs_);
    const uint32_t clock = song ? song->sid.clockHz() : sid::kPalClockHz;
    for (int i = 0; i < kChips; ++i)
        chips_[i].init(song ? song->sid.model[i] : sid::Model::k8580, clock, fs_);
    player_.init(song, this, this);
    pcm_.setSampleRate(fs_ / 1000);
    mixer_.reset();
    fx_.init(fxMem_, fxFrames_, fs_);
    toTick16_ = 0;
    if (song) applySongConfig();
    applied_ = req_;
    appliedVersion_ = version_.load();
    mixer_.setBalance(req_.balance);
    mixer_.setMasterGain(float(req_.volume) / 100.0f);
}

// --- main loop side ----------------------------------------------------------

void Engine::push(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) >= kRing) return;   // full: drop
    ring_[h % kRing] = Cmd{type, a, b, c};
    head_.store(h + 1, std::memory_order_release);
}

void Engine::setModel(int chip, sid::Model m) {
    if (chip < 0 || chip >= kChips) return;
    req_.model[chip] = m;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setClock(SidClock c) {
    req_.clock = c;
    player_.setClock(c == SidClock::kNtsc ? sid::kNtscClockHz : sid::kPalClockHz);
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setBalance(uint8_t width) {
    req_.balance = width > 100 ? 100 : width;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setMasterVolume(uint8_t percent) {
    req_.volume = percent > 100 ? 100 : percent;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setMuted(int ch, bool on) {
    if (ch < 0 || ch > kChannels) return;   // kChannels = the drum column
    muteMask_ = uint8_t(on ? (muteMask_ | (1u << ch)) : (muteMask_ & ~(1u << ch)));
    req_.mute = muteMask_;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setSolo(int ch, bool on) {
    if (ch < 0 || ch > kChannels) return;   // kChannels = the drum column
    soloMask_ = uint8_t(on ? (soloMask_ | (1u << ch)) : (soloMask_ & ~(1u << ch)));
    req_.solo = soloMask_;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setFxMemory(int16_t* mem, uint32_t frames) {
    fxMem_ = mem;
    fxFrames_ = mem ? frames : 0;
    fx_.init(fxMem_, fxFrames_, fs_);
}

void Engine::setKitMemory(int16_t* mem, uint32_t samples) {
    kit_.init(mem, samples, fs_ / 1000);
    kit_.synthesize();
}

void Engine::drum(uint8_t slot) {
    // The drum column mutes / solos like a seventh track (bit 6).
    if ((applied_.mute & 0x40) || (applied_.solo && !(applied_.solo & 0x40))) return;
    pcm_.trigger(kit_.slot(slot), slot);
}

void Engine::setEffects(bool on) {
    req_.fx = on;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setStress(bool on) {
    stressReq_ = on;
    req_.stress = on;
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::applySongConfig() {
    if (!song_) return;
    req_.model[0] = song_->sid.model[0];
    req_.model[1] = song_->sid.model[1];
    req_.balance = song_->sid.balance;
    setClock(song_->sid.clock);   // bumps the version
}

void Engine::hold() {
    hold_.store(true, std::memory_order_seq_cst);
    // On the device the callback is an ISR and cannot be "inside" while the
    // main loop runs; on the host it is another thread. Bounded wait.
    for (uint32_t i = 0; i < 50000000u && inFill_.load(std::memory_order_seq_cst); ++i) {
    }
}

void Engine::release() { hold_.store(false, std::memory_order_seq_cst); }

Profile Engine::takeProfile() {
    Profile p = prof_;   // word reads; a torn value is one stale block
    prof_ = Profile{};
    return p;
}

// --- audio side --------------------------------------------------------------

void Engine::sidWrite(uint8_t chip, uint8_t reg, uint8_t val) {
    if (chip >= kChips) return;
    chips_[chip].write(reg, val);
    if (tap_) tap_->onWrite(samples_, chip, reg, val);
}

void Engine::drain() {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    for (; t != h; ++t) {
        const Cmd& c = ring_[t % kRing];
        switch (c.type) {
            case kCmdPlaySong:
                player_.resetPerformLanes(autoReset_);
                player_.playSong(c.a, c.b);
                toTick16_ = 0;   // first row on this block's first sample
                break;
            case kCmdPlayPattern:
                player_.playPattern(c.a, c.b);
                toTick16_ = 0;
                break;
            case kCmdStop: player_.stop(); break;
            case kCmdNoteOn: player_.noteOn(c.a, c.b, c.c); break;
            case kCmdNoteOff: player_.noteOff(c.a); break;
            case kCmdDrum: pcm_.trigger(kit_.slot(c.a), c.a); break;
            case kCmdPanic:
                pcm_.stopAll();
                player_.reset();
                for (sid::Chip& ch : chips_) ch.reset();
                mixer_.reset();
                break;
        }
    }
    tail_.store(t, std::memory_order_release);
}

void Engine::applySettings() {
    const uint32_t v = version_.load(std::memory_order_acquire);
    if (v == appliedVersion_) return;
    appliedVersion_ = v;
    const Settings s = req_;
    const uint32_t clock = s.clock == SidClock::kNtsc ? sid::kNtscClockHz : sid::kPalClockHz;
    for (int i = 0; i < kChips; ++i) {
        chips_[i].setModel(s.model[i]);
        chips_[i].setClock(clock);
    }
    mixer_.setBalance(s.balance);
    mixer_.setMasterGain(float(s.volume) / 100.0f);
    for (int ch = 0; ch < kChannels; ++ch) {
        const bool m = ((s.mute >> ch) & 1) || (s.solo && !((s.solo >> ch) & 1));
        chips_[ch / 3].setVoiceMuted(ch % 3, m);
    }
    if (s.stress != applied_.stress) {
        if (s.stress) {
            applyStress();
        } else {
            player_.reset();
            for (sid::Chip& c : chips_) c.reset();
        }
    }
    applied_ = s;
}

void Engine::applyStress() {
    // Worst case for this engine: polyBLEP saw / pulse on most voices (the
    // band-limiting is the costly path), noise on one per chip, all voices
    // through the filter, both chips at full resonance.
    static const uint8_t kWave[3] = {sid::kPulse, sid::kSaw, sid::kNoise};
    player_.reset();
    for (int c = 0; c < kChips; ++c) {
        sid::Chip& chip = chips_[c];
        chip.reset();
        for (int v = 0; v < 3; ++v) {
            const uint8_t b = uint8_t(v * sid::kVoiceStride);
            const uint16_t f = uint16_t(0x1000 + 0x2345 * v + 0x800 * c);
            chip.write(uint8_t(b + sid::kFreqLo), uint8_t(f));
            chip.write(uint8_t(b + sid::kFreqHi), uint8_t(f >> 8));
            chip.write(uint8_t(b + sid::kPwLo), 0x00);
            chip.write(uint8_t(b + sid::kPwHi), 0x08);
            chip.write(uint8_t(b + sid::kAD), 0x00);
            chip.write(uint8_t(b + sid::kSR), 0xF0);
            chip.write(uint8_t(b + sid::kControl), uint8_t(kWave[v] | sid::kGate));
        }
        chip.write(sid::kFcLo, 0x07);
        chip.write(sid::kFcHi, 0x60);
        chip.write(sid::kResFilt, 0xF7);
        chip.write(sid::kModeVol, 0x1F);
    }
}

void Engine::copyPanned(int chip, uint32_t pos, uint32_t n) {
    if (!chips_[chip].anyPanned()) return;
    const float* l = chips_[chip].pannedL();
    const float* r = chips_[chip].pannedR();
    for (uint32_t i = 0; i < n; ++i) {
        panBuf_[chip][0][pos + i] = l[i];
        panBuf_[chip][1][pos + i] = r[i];
    }
}

void Engine::fill(int16_t* out, uint32_t frames) {
    inFill_.store(true, std::memory_order_seq_cst);
    if (hold_.load(std::memory_order_seq_cst) || !song_) {
        std::memset(out, 0, frames * 2 * sizeof(int16_t));
        inFill_.store(false, std::memory_order_seq_cst);
        return;
    }
    if (frames > kBlock) frames = kBlock;
    const uint32_t t0 = now();
    for (uint32_t& c : cur_) c = 0;
    applySettings();
    drain();
    const bool stress = applied_.stress;
    // Per-voice pan (M8): linear like the SID balance, 0 = follow the chip.
    for (int ch = 0; ch < kChannels; ++ch) {
        const int p = song_->mod.pan[ch];
        const float gl = p > 0 ? 1.0f - float(p) / 100.0f : 1.0f;
        const float gr = p < 0 ? 1.0f + float(p) / 100.0f : 1.0f;
        chips_[ch / 3].setVoicePan(ch % 3, p != 0, gl, gr);
    }
    uint32_t pos = 0;
    while (pos < frames) {
        if (toTick16_ < 0x10000u) {
            if (!stress) {
                const uint32_t tp = now();
                player_.tick();
                account(Profile::kPlayer, now() - tp);
            }
            toTick16_ += player_.samplesPerTick16(fs_);
        }
        uint32_t n = toTick16_ >> 16;
        if (n > frames - pos) n = frames - pos;
        uint32_t t = now();
        chips_[0].renderVoices(int(n));
        uint32_t u = now();
        account(Profile::kSid1Voices, u - t);
        chips_[0].renderFilter(int(n), bufA_ + pos);
        copyPanned(0, pos, n);
        t = now();
        account(Profile::kSid1Filter, t - u);
        chips_[1].renderVoices(int(n));
        u = now();
        account(Profile::kSid2Voices, u - t);
        chips_[1].renderFilter(int(n), bufB_ + pos);
        copyPanned(1, pos, n);
        t = now();
        account(Profile::kSid2Filter, t - u);
        toTick16_ -= n << 16;
        pos += n;
        samples_ += n;
    }
    uint32_t tm = now();
    mixer_.mixToBus(bufA_, bufB_, int(frames), busL_, busR_);
    for (int c = 0; c < kChips; ++c)
        if (chips_[c].anyPanned()) mixer_.addPanned(c, panBuf_[c][0], panBuf_[c][1], int(frames), busL_, busR_);
    account(Profile::kMixer, now() - tm);
    if (pcm_.active()) {
        const uint32_t tp = now();
        pcm_.setVolume(song_->drumVolume);
        pcm_.render(busL_, busR_, int(frames));
        account(Profile::kPcm, now() - tp);
    }
    if (applied_.fx && song_->fx.any()) {
        const uint32_t tf = now();
        fx_.configure(song_->fx, player_.samplesPerTick16(fs_) >> 16);
        fx_.process(busL_, busR_, int(frames));
        account(Profile::kEffects, now() - tf);
    }
    tm = now();
    mixer_.toOutput(busL_, busR_, int(frames), out);
    for (int c = 0; c < kChips; ++c)
        if (mixer_.chipPeak(c) > meters_.sid[c]) meters_.sid[c] = mixer_.chipPeak(c);
    if (pcm_.active() && pcm_.peak() > meters_.drums) meters_.drums = pcm_.peak();
    if (float(mixer_.peak()) * (1.0f / 32767.0f) > meters_.out) meters_.out = float(mixer_.peak()) * (1.0f / 32767.0f);
    const uint32_t te = now();
    account(Profile::kMixer, te - tm);
    account(Profile::kTotal, te - t0);
    for (int s = 0; s < Profile::kSections; ++s) {
        prof_.last[s] = cur_[s];
        if (cur_[s] > prof_.peak[s]) prof_.peak[s] = cur_[s];
        prof_.sum[s] += cur_[s];
    }
    ++prof_.blocks;
    inFill_.store(false, std::memory_order_seq_cst);
}

}  // namespace sidt
