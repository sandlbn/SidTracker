// SIDTracker -- GoatTracker 2 file formats, raw (docs/goattracker-format.md).
//
// Byte-level readers and writers for .sng (GTS5) and .ins (GTI5), written
// from the GoatTracker 2.77 readme §6 and checked against files produced by
// GoatTracker itself. No GoatTracker code is used (DECISIONS.md 2, 3).
#pragma once

#include <cstdint>

namespace sidt {
namespace goat {

constexpr int kSubtunes = 32;
constexpr int kOrderMax = 256;      // 254 entries + RST + restart position
constexpr int kPatterns = 208;
constexpr int kPatternRows = 128;   // rows before the end mark
constexpr int kInstruments = 64;    // 1..63 stored
constexpr int kTableRows = 255;
constexpr int kNameLen = 32;

// Pattern note bytes.
constexpr uint8_t kNoteFirst = 0x60;   // C-0
constexpr uint8_t kNoteLast = 0xBC;    // G#7
constexpr uint8_t kRest = 0xBD;
constexpr uint8_t kKeyOff = 0xBE;
constexpr uint8_t kKeyOn = 0xBF;
constexpr uint8_t kPatternEnd = 0xFF;
// Order list bytes.
constexpr uint8_t kOrderRepeat = 0xD0;     // D0..DF: next pattern plays (low nibble + 1) times
constexpr uint8_t kOrderTranspose = 0xE0;  // E0..FE: transpose = value - F0
constexpr uint8_t kOrderEnd = 0xFF;        // RST, followed by the restart position

struct Row {
    uint8_t note = kRest, inst = 0, cmd = 0, param = 0;
};

struct Instrument {
    uint8_t ad = 0, sr = 0, wavePtr = 0, pulsePtr = 0, filterPtr = 0;
    uint8_t vibParam = 0, vibDelay = 0, gateTimer = 0, firstWave = 0;
    char name[17] = {};
};

struct Tables {
    uint8_t len[4] = {};                 // wave, pulse, filter, speed
    uint8_t left[4][kTableRows] = {};
    uint8_t right[4][kTableRows] = {};
};

struct Song {
    char name[kNameLen + 1] = {}, author[kNameLen + 1] = {}, copyright[kNameLen + 1] = {};
    uint8_t subtunes = 0;
    // Order list bytes per subtune and channel, up to and including the RST
    // mark; orderRestart = the byte after it.
    uint8_t orderLen[kSubtunes][3] = {};
    uint8_t order[kSubtunes][3][kOrderMax] = {};
    uint8_t orderRestart[kSubtunes][3] = {};
    uint8_t instruments = 0;             // highest stored instrument
    Instrument inst[kInstruments];
    Tables tables;
    uint8_t patterns = 0;
    uint8_t rows[kPatterns] = {};        // rows per pattern (end mark excluded)
    Row pattern[kPatterns][kPatternRows];
};

enum class Result : uint8_t { kOk, kTruncated, kBadMagic, kOldFormat, kCorrupt, kTooLarge };
const char* resultText(Result r);

// Byte sources / sinks (bounded memory views; the app adapts IStorage).
struct Bytes {
    const uint8_t* data;
    uint32_t size;
};
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool put(const void* d, uint32_t n) = 0;
};

Result readSong(Bytes in, Song& out);
bool writeSong(const Song& s, Sink& out);

// .ins: one instrument with the table rows it was saved with.
struct InsFile {
    Instrument inst;
    Tables tables;
};
Result readIns(Bytes in, InsFile& out);
bool writeIns(const InsFile& f, Sink& out);

}  // namespace goat
}  // namespace sidt
