// SIDTracker -- native project file ".sidt" (PLAN.md §15, docs/project-format.md).
//
// "SIDT", u16 format version, u16 flags, then tagged chunks (4-byte tag, u32
// little-endian payload length, payload) ending with "END ". Every field is
// written byte by byte (never a C++ struct image). Readers skip unknown
// chunks, and records carry their own field counts, so later versions can
// append fields without breaking older files.
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {
namespace project {

constexpr uint16_t kFormatVersion = 1;
constexpr char kExtension[] = ".sidt";

class Writer {
public:
    virtual ~Writer() = default;
    virtual bool write(const void* data, uint32_t len) = 0;
};

class Reader {
public:
    virtual ~Reader() = default;
    virtual int32_t read(void* data, uint32_t len) = 0;   // bytes read, < 0 error
};

enum class Result : uint8_t { kOk, kIoError, kBadMagic, kNewerVersion, kCorrupt };
const char* resultText(Result r);

// Writes the whole song (non-empty and referenced patterns only).
Result save(const Song& song, Writer& w);
// Clears `song` (keeps song.patterns, which must be set) and reads it.
Result load(Song& song, Reader& r);

}  // namespace project
}  // namespace sidt
