// SIDTracker -- PCM drums (PLAN.md §12, M8): "SID + lightweight drums".
//
// A kit of 16 one-shot samples: slots 1-8 are synthesised at boot (our
// own sounds, generated procedurally), any slot can be replaced by a WAV
// file from the card (/SIDTracker/Kit/NNname.wav, NN = 01..16). Four voices
// play them on the stereo bus; the drum column of a pattern triggers them.
#pragma once

#include <cstdint>

namespace sidt {
namespace audio {

constexpr int kKitSlots = 16;
constexpr int kKitNameLen = 12;

struct Sample {
    const int16_t* data = nullptr;   // mono
    uint32_t frames = 0;
    uint32_t rateHz = 44100;
    char name[kKitNameLen + 1] = {};
};

// Sample memory and slot table. Main loop only (build / load); the audio
// side reads the slots, so loads happen with the engine on hold.
class Kit {
public:
    void init(int16_t* mem, uint32_t capSamples, uint32_t sampleRateHz);
    // Built-in kit into slots 1-8 (frees everything first).
    void synthesize();
    // A WAV file (PCM 8/16-bit, mono or stereo, any rate) into `slot`
    // (1..16). Returns false (slot unchanged) on an unsupported or too large
    // file. Needs the memory above what is used.
    bool loadWav(int slot, const uint8_t* bytes, uint32_t size, const char* name);
    const Sample& slot(int s) const { return slots_[(s - 1) & (kKitSlots - 1)]; }
    uint32_t usedSamples() const { return used_; }
    uint32_t capacity() const { return cap_; }

private:
    int16_t* alloc(uint32_t n);
    int16_t* mem_ = nullptr;
    uint32_t cap_ = 0, used_ = 0, fs_ = 44100;
    Sample slots_[kKitSlots];
};

class Pcm {
public:
    static constexpr int kVoices = 4;
    void setSampleRate(uint32_t hz) { fs_ = hz; }
    void setVolume(uint8_t percent) { gain_ = float(percent) / 100.0f; }
    // Audio context. Slot 4 (closed hat) chokes slot 5 (open hat).
    void trigger(const Sample& s, int slot);
    void stopAll();
    void render(float* L, float* R, int n);
    int active() const;
    float peak() const { return peak_; }   // last render, bus units

private:
    struct Voice {
        const Sample* s = nullptr;
        int slot = 0;
        uint64_t pos = 0;    // 32.32 frames
        uint64_t step = 0;
    };
    Voice v_[kVoices];
    int next_ = 0;
    uint32_t fs_ = 44100;
    float gain_ = 0.8f;
    float peak_ = 0.0f;
};

}  // namespace audio
}  // namespace sidt
