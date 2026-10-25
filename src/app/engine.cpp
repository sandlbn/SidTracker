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

void Engine::setAsidPorts(uint8_t mask) {
    req_.asidPorts = uint8_t(mask & 3);
    version_.fetch_add(1, std::memory_order_release);
}

void Engine::setClockPorts(uint8_t mask) {
    req_.clockPorts = uint8_t(mask & 3);
    version_.fetch_add(1, std::memory_order_release);
}

bool Engine::midiSend(uint8_t ports, const uint8_t* d, uint32_t n) {
    if (!midiOut_ || !n) return true;
    bool ok = true;
    if (ports & 1) ok = midiOut_->send(ot::MidiPortId::kDin, d, n) && ok;
    if (ports & 2) ok = midiOut_->send(ot::MidiPortId::kUsb, d, n) && ok;
    return ok;
}

void Engine::asidImage() {
    if (!applied_.asidPorts) return;
    for (int c = 0; c < kChips; ++c)
        for (uint8_t r = 0; r < 25; ++r) asid_.set(uint8_t(c), r, chips_[c].shadow(r));
}

void Engine::midiTick() {
    // Clock: 6 per row, so 6 / speed per tick (speed 6: one per tick).
    const bool playing = !external_ && player_.playing() && !applied_.stress;
    if (applied_.clockPorts) {
        static const uint8_t kStart = 0xFA, kStop = 0xFC, kClock = 0xF8;
        if (playing && !wasPlaying_) {
            midiSend(applied_.clockPorts, &kStart, 1);
            clockAcc_ = 0;
        } else if (!playing && wasPlaying_) {
            midiSend(applied_.clockPorts, &kStop, 1);
        }
        if (playing) {
            const uint8_t speed = player_.speed() ? player_.speed() : 6;
            clockAcc_ = uint8_t(clockAcc_ + 6);
            while (clockAcc_ >= speed) {
                midiSend(applied_.clockPorts, &kClock, 1);
                clockAcc_ = uint8_t(clockAcc_ - speed);
            }
        }
    }
    wasPlaying_ = playing;
    if (asidStartPending_) {   // a port that refused the start (USB not enumerated yet): retry
        uint8_t m[4];
        const uint32_t n = midi::AsidEncoder::start(m);
        for (uint8_t bit = 1; bit <= 2; bit <<= 1)
            if ((asidStartPending_ & bit) && midiSend(bit, m, n)) {
                asidStartPending_ = uint8_t(asidStartPending_ & ~bit);
                asidImage();   // the receiver starts from the full register image
            }
    }
    if (applied_.asidPorts && asid_.pending()) {
        // One message per chip; a refused one keeps its registers for the
        // next tick, so no write (gate off, volume) is lost for good.
        uint8_t buf[midi::AsidEncoder::kMaxMessage];
        for (int c = 0; c < midi::AsidEncoder::kChips; ++c) {
            uint32_t taken = 0;
            const uint32_t n = asid_.build(c, buf, taken);
            if (n && !midiSend(applied_.asidPorts, buf, n)) asid_.restore(c, taken);
        }
    }
}

void Engine::setSidCopy(bool on) {
    if (req_.sidCopy == on) return;
    req_.sidCopy = on;
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
    if (applied_.sidCopy) {
        if (chip == 1) return;   // SID 2 follows SID 1 instead
        chips_[1].write(reg, val);
        if (applied_.asidPorts) asid_.write(1, reg, val);
    }
    chips_[chip].write(reg, val);
    if (applied_.asidPorts) asid_.write(chip, reg, val);
    if (tap_) tap_->onWrite(samples_, chip, reg, val);
}

void Engine::startExternal(uint32_t tickMilliHz) {
    extRate_.store(tickMilliHz ? tickMilliHz : 50125, std::memory_order_release);
    extReq_ = true;
    ++extGen_;
    push(kCmdExternal, 1, extGen_);
}

bool Engine::pushFrame(const c64::SidWrite* w, int n) {
    const uint32_t h = fHead_.load(std::memory_order_relaxed);
    if (h - fTail_.load(std::memory_order_acquire) >= kFrames) return false;
    Frame& f = frames_[h % kFrames];
    if (n > c64::SidPlayer::kMaxWrites) n = c64::SidPlayer::kMaxWrites;
    for (int i = 0; i < n; ++i) f.w[i] = w[i];
    f.n = uint8_t(n);
    f.gen = extGen_;
    fHead_.store(h + 1, std::memory_order_release);
    return true;
}

void Engine::applyFrame() {
    uint32_t t = fTail_.load(std::memory_order_relaxed);
    const uint32_t h = fHead_.load(std::memory_order_acquire);
    // Frames of an earlier tune are skipped; a frame of a newer one (its start
    // command not drained yet) waits.
    while (t != h && int8_t(uint8_t(frames_[t % kFrames].gen - activeGen_)) < 0) ++t;
    if (t != h && frames_[t % kFrames].gen == activeGen_) {
        const Frame& f = frames_[t % kFrames];
        for (int i = 0; i < f.n; ++i) sidWrite(f.w[i].chip, f.w[i].reg, f.w[i].val);
        ++t;
    }   // else: a late frame, the registers stay as they are
    fTail_.store(t, std::memory_order_release);
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
            case kCmdExternal:
                external_ = c.a != 0;
                player_.reset();
                pcm_.stopAll();
                for (sid::Chip& ch : chips_) ch.reset();
                asidImage();   // the receiver resets too
                if (external_) activeGen_ = c.b;   // older frames are skipped by applyFrame()
                extTick16_ = uint32_t(uint64_t(fs_) * 65536u / extRate_.load(std::memory_order_acquire));
                toTick16_ = 0;
                break;
            case kCmdPanic:
                pcm_.stopAll();
                player_.reset();
                for (sid::Chip& ch : chips_) ch.reset();
                asidImage();
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
        chips_[i].setModel(s.sidCopy ? s.model[0] : s.model[i]);
        chips_[i].setClock(clock);
    }
    if (!s.sidCopy && applied_.sidCopy) {
        // The copy ends: SID 2 is silenced and the player writes its own
        // SID 2 state again (notes recorded meanwhile sound).
        chips_[1].reset();
        if (s.asidPorts)
            for (uint8_t r = 0; r < 25; ++r) asid_.set(1, r, 0);
        player_.rewriteChip(1);
    }
    if (s.sidCopy && !applied_.sidCopy) {   // the copy starts from SID 1's registers
        for (uint8_t r = 0; r < 25; ++r) {
            chips_[1].write(r, chips_[0].shadow(r));
            if (s.asidPorts) asid_.set(1, r, chips_[0].shadow(r));
        }
    }
    // A copy is the same sound on both chips: one chip per side (centred,
    // not summed into twice the level).
    mixer_.setBalance(s.sidCopy ? 100 : s.balance);
    mixer_.setMasterGain(float(s.volume) / 100.0f);
    if (s.asidPorts != applied_.asidPorts) {   // ASID start / stop on the ports that change
        uint8_t m[4];
        const uint8_t added = uint8_t(s.asidPorts & ~applied_.asidPorts);
        midiSend(uint8_t(applied_.asidPorts & ~s.asidPorts), m, midi::AsidEncoder::stop(m));
        asidStartPending_ = uint8_t((asidStartPending_ | added) & s.asidPorts);   // sent by midiTick
        if (!s.asidPorts) asid_.clear();   // forget a half frame
    }
    if (s.clockPorts != applied_.clockPorts && wasPlaying_) {   // Stop / Start on the ports that change
        static const uint8_t kStart = 0xFA, kStop = 0xFC;
        midiSend(uint8_t(applied_.clockPorts & ~s.clockPorts), &kStop, 1);
        const uint8_t added = uint8_t(s.clockPorts & ~applied_.clockPorts);
        if (added) {
            midiSend(added, &kStart, 1);
            clockAcc_ = 0;
        }
    }
    for (int ch = 0; ch < kChannels; ++ch) {
        const int src = s.sidCopy && ch >= 3 ? ch - 3 : ch;   // the copy mutes with its original
        const bool m = ((s.mute >> src) & 1) || (s.solo && !((s.solo >> src) & 1));
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
        const int src = applied_.sidCopy && ch >= 3 ? ch - 3 : ch;   // the copy pans / levels with its original
        const int p = external_ ? 0 : song_->mod.pan[src];
        // While copying, a panned voice plays on both chips: each at half level.
        const float k = applied_.sidCopy ? 0.5f : 1.0f;
        const float gl = k * (p > 0 ? 1.0f - float(p) / 100.0f : 1.0f);
        const float gr = k * (p < 0 ? 1.0f + float(p) / 100.0f : 1.0f);
        chips_[ch / 3].setVoicePan(ch % 3, p != 0, gl, gr);
        chips_[ch / 3].setVoiceGain(ch % 3, external_ ? 1.0f : float(song_->mix.volume[src]) * 0.01f);
    }
    uint32_t pos = 0;
    while (pos < frames) {
        if (toTick16_ < 0x10000u) {
            if (external_) {
                applyFrame();
                toTick16_ += extTick16_;
            } else {
                if (!stress) {
                    const uint32_t tp = now();
                    player_.tick();
                    account(Profile::kPlayer, now() - tp);
                }
                const uint32_t ext = extTick16Req_.load(std::memory_order_acquire);
                toTick16_ += ext ? ext : player_.samplesPerTick16(fs_);
            }
            midiTick();
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
    if (!external_ && applied_.fx && song_->fx.any()) {
        const uint32_t tf = now();
        fx_.configure(song_->fx, (player_.samplesPerTick16(fs_) >> 16) * song_->multiSpeed());   // delay in frames
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
