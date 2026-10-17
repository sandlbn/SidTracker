// SIDTracker -- PCM drums (pcm.h).
#include "audio/pcm.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sidt {
namespace audio {

namespace {

// Deterministic noise (no library rand).
struct Noise {
    uint32_t s = 0x12345678u;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(int32_t(s)) * (1.0f / 2147483648.0f);
    }
};

int16_t to16(float v) {
    const float x = v * 32767.0f;
    return int16_t(x > 32767.0f ? 32767 : (x < -32767.0f ? -32767 : int32_t(x)));
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

}  // namespace

void Kit::init(int16_t* mem, uint32_t capSamples, uint32_t sampleRateHz) {
    mem_ = mem;
    cap_ = mem ? capSamples : 0;
    used_ = 0;
    fs_ = sampleRateHz ? sampleRateHz : 44100;
    for (Sample& s : slots_) s = Sample{};
}

int16_t* Kit::alloc(uint32_t n) {
    if (!mem_ || used_ + n > cap_) return nullptr;
    int16_t* p = mem_ + used_;
    used_ += n;
    return p;
}

void Kit::synthesize() {
    used_ = 0;
    for (Sample& s : slots_) s = Sample{};
    const float fs = float(fs_);
    const float kTwoPi = 6.2831853f;
    auto make = [&](int slot, const char* name, float seconds, auto fn) {
        const uint32_t n = uint32_t(seconds * fs);
        int16_t* d = alloc(n);
        if (!d) return;
        Noise noise;
        float state[4] = {};
        for (uint32_t i = 0; i < n; ++i) d[i] = to16(fn(float(i) / fs, noise, state));
        Sample& s = slots_[slot - 1];
        s.data = d;
        s.frames = n;
        s.rateHz = fs_;
        std::snprintf(s.name, sizeof s.name, "%s", name);
    };
    // 1 kick: sine sweeping 160 -> 45 Hz, exponential decay, click.
    make(1, "KICK", 0.45f, [&](float t, Noise&, float* st) {
        const float f = 45.0f + 115.0f * std::exp(-t * 28.0f);
        st[0] += kTwoPi * f / fs;
        const float click = t < 0.002f ? 0.6f * (1.0f - t / 0.002f) : 0.0f;
        return 0.95f * std::sin(st[0]) * std::exp(-t * 7.0f) + click;
    });
    // 2 snare: 185 Hz body + high-passed noise.
    make(2, "SNARE", 0.28f, [&](float t, Noise& nz, float* st) {
        st[0] += kTwoPi * 185.0f / fs;
        const float n = nz.next();
        const float hp = n - st[1];
        st[1] = n * 0.4f + st[1] * 0.6f;
        return 0.45f * std::sin(st[0]) * std::exp(-t * 25.0f) + 0.6f * hp * std::exp(-t * 14.0f);
    });
    // 3 clap: three short noise bursts and a tail, band-limited.
    make(3, "CLAP", 0.32f, [&](float t, Noise& nz, float* st) {
        float env = 0.0f;
        for (int k = 0; k < 3; ++k) {
            const float tk = t - 0.011f * float(k);
            if (tk >= 0.0f && tk < 0.011f) env = std::exp(-tk * 180.0f);
        }
        if (t >= 0.033f) env = 0.7f * std::exp(-(t - 0.033f) * 16.0f);
        const float n = nz.next();
        st[0] = st[0] * 0.5f + n * 0.5f;           // low pass
        const float bp = st[0] - st[1];
        st[1] = st[1] * 0.92f + st[0] * 0.08f;     // minus lower band
        return 1.6f * bp * env;
    });
    // 4 closed hat, 5 open hat: high-passed noise with metallic partials.
    auto hat = [&](float decay) {
        return [&, decay](float t, Noise& nz, float* st) {
            const float n = nz.next();
            const float hp = n - st[0];
            st[0] = st[0] * 0.3f + n * 0.7f;
            float m = 0.0f;
            const float fr[3] = {3170.0f, 4930.0f, 6010.0f};
            for (int k = 0; k < 3; ++k) m += std::sin(kTwoPi * fr[k] * t) > 0.0f ? 0.1f : -0.1f;
            return (0.5f * hp + 0.4f * m) * std::exp(-t * decay);
        };
    };
    make(4, "HAT CLOSED", 0.07f, hat(70.0f));
    make(5, "HAT OPEN", 0.45f, hat(9.0f));
    // 6 tom: sine 210 -> 120 Hz.
    make(6, "TOM", 0.38f, [&](float t, Noise&, float* st) {
        st[0] += kTwoPi * (120.0f + 90.0f * std::exp(-t * 18.0f)) / fs;
        return 0.85f * std::sin(st[0]) * std::exp(-t * 9.0f);
    });
    // 7 rim: two short resonances.
    make(7, "RIM", 0.06f, [&](float t, Noise&, float*) {
        return (0.6f * std::sin(kTwoPi * 1700.0f * t) + 0.4f * std::sin(kTwoPi * 460.0f * t)) * std::exp(-t * 90.0f);
    });
    // 8 crash: noise plus inharmonic squares, long decay.
    make(8, "CRASH", 1.1f, [&](float t, Noise& nz, float* st) {
        const float n = nz.next();
        const float hp = n - st[0];
        st[0] = st[0] * 0.5f + n * 0.5f;
        float m = 0.0f;
        const float fr[4] = {2730.0f, 3810.0f, 5020.0f, 7160.0f};
        for (int k = 0; k < 4; ++k) m += std::sin(kTwoPi * fr[k] * t) > 0.0f ? 0.06f : -0.06f;
        return (0.55f * hp + m) * std::exp(-t * 3.2f);
    });
}

bool Kit::loadWav(int slot, const uint8_t* b, uint32_t size, const char* name) {
    if (slot < 1 || slot > kKitSlots || size < 12 || std::memcmp(b, "RIFF", 4) || std::memcmp(b + 8, "WAVE", 4))
        return false;
    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    uint32_t dataLen = 0;
    for (uint32_t p = 12; p + 8 <= size;) {
        const uint32_t len = le32(b + p + 4);
        const uint8_t* body = b + p + 8;
        if (p + 8 + len > size) break;
        if (!std::memcmp(b + p, "fmt ", 4) && len >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            rate = le32(body + 4);
            bits = le16(body + 14);
        } else if (!std::memcmp(b + p, "data", 4)) {
            data = body;
            dataLen = len;
        }
        p += 8 + len + (len & 1);
    }
    if (format != 1 || !data || !channels || channels > 2 || (bits != 8 && bits != 16) || rate < 4000 || rate > 96000)
        return false;
    const uint32_t frameBytes = uint32_t(channels) * (bits / 8);
    uint32_t frames = dataLen / frameBytes;
    const uint32_t maxFrames = rate * 4;   // 4 s per slot
    if (frames > maxFrames) frames = maxFrames;
    int16_t* d = alloc(frames);
    if (!d) return false;
    for (uint32_t i = 0; i < frames; ++i) {
        int32_t acc = 0;
        for (uint16_t c = 0; c < channels; ++c) {
            const uint8_t* s = data + i * frameBytes + c * (bits / 8);
            acc += bits == 16 ? int16_t(le16(s)) : (int32_t(s[0]) - 128) * 256;
        }
        d[i] = int16_t(acc / channels);
    }
    Sample& s = slots_[slot - 1];
    s.data = d;
    s.frames = frames;
    s.rateHz = rate;
    std::snprintf(s.name, sizeof s.name, "%s", name);
    return true;
}

void Pcm::trigger(const Sample& s, int slot) {
    if (!s.data || !s.frames) return;
    if (slot == 4)
        for (Voice& v : v_)
            if (v.s && v.slot == 5) v.s = nullptr;   // closed hat chokes open hat
    // A free voice, else the oldest (round robin).
    int k = -1;
    for (int i = 0; i < kVoices && k < 0; ++i)
        if (!v_[(next_ + i) % kVoices].s) k = (next_ + i) % kVoices;
    if (k < 0) k = next_;
    next_ = (k + 1) % kVoices;
    Voice& v = v_[k];
    v.s = &s;
    v.slot = slot;
    v.pos = 0;
    v.step = (uint64_t(s.rateHz) << 32) / (fs_ ? fs_ : 44100);
}

void Pcm::stopAll() {
    for (Voice& v : v_) v.s = nullptr;
}

int Pcm::active() const {
    int n = 0;
    for (const Voice& v : v_) n += v.s != nullptr;
    return n;
}

void Pcm::render(float* L, float* R, int n) {
    const float g = gain_ * 0.6f * (1.0f / 32768.0f);
    float pk = 0.0f;
    for (Voice& v : v_) {
        if (!v.s) continue;
        const int16_t* d = v.s->data;
        const uint32_t frames = v.s->frames;
        uint64_t pos = v.pos;
        const uint64_t step = v.step;
        for (int i = 0; i < n; ++i) {
            const uint32_t ip = uint32_t(pos >> 32);
            if (ip + 1 >= frames) {
                v.s = nullptr;
                break;
            }
            const float fr = float(uint32_t(pos)) * (1.0f / 4294967296.0f);
            const float x = (float(d[ip]) + (float(d[ip + 1]) - float(d[ip])) * fr) * g;
            L[i] += x;
            R[i] += x;
            const float ax = x < 0.0f ? -x : x;
            if (ax > pk) pk = ax;
            pos += step;
        }
        v.pos = pos;
    }
    peak_ = pk;
}

}  // namespace audio
}  // namespace sidt
