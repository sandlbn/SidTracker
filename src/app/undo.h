// SIDTracker -- undo / redo for song edits.
//
// A mirror of the song (its settings, instruments and tables, and every
// pattern) lives in external memory. After an edit burst (no change for
// kQuietMs) the song is compared with the mirror; the old copy of every
// object that changed (the song core or a pattern) goes into a history ring
// as one step, and the mirror catches up. Undo swaps a step's objects with
// the live ones (so the same entries then serve redo); a new edit after an
// undo drops the redo entries. Loading or starting a song resets it.
#pragma once

#include <cstdint>

#include "tracker/song/song.h"

namespace sidt {

class Undo {
public:
    static constexpr uint32_t kQuietMs = 300;
    static constexpr int kEntries = 160;   // objects in the history (64+ steps)
    // Memory for the mirror and the ring (plus one swap slot).
    static constexpr uint32_t kSlot = ((sizeof(Pattern) > sizeof(Song) ? sizeof(Pattern) : sizeof(Song)) + 15) & ~15u;
    static constexpr uint32_t kBytes = sizeof(Song) + sizeof(Pattern) * kPatternCount + kSlot * (kEntries + 1);

    void init(uint8_t* mem, uint32_t bytes, Song* song);
    bool ready() const { return mirror_ != nullptr; }
    // Mirror = the live song, history cleared (song loaded or new).
    void reset();
    // An edit happened (App::markDirty); recorded once things are quiet.
    void touched(uint32_t nowMs);
    // Records a pending edit burst after kQuietMs (call every loop pass;
    // `force` records now, e.g. right before an undo).
    void service(uint32_t nowMs, bool force = false);
    bool undo();   // false: nothing to undo
    bool redo();
    int undoSteps() const;
    int redoSteps() const;

private:
    struct Entry {
        uint32_t step;
        int16_t object;   // -1 song core, 0..127 pattern
    };
    uint8_t* slot(int ring) { return ring_ + uint32_t(ring) * kSlot; }
    void record();
    void swapIn(int ring);
    int ringIndex(int k) const { return (first_ + k) % kEntries; }

    Song* song_ = nullptr;
    Song* mirror_ = nullptr;       // the core (patterns pointer: the mirror's)
    Pattern* mirrorPats_ = nullptr;
    uint8_t* ring_ = nullptr;
    Entry entries_[kEntries] = {};
    int first_ = 0;      // oldest entry
    int count_ = 0;      // entries held
    int cursor_ = 0;     // entries [0, cursor_) can be undone, [cursor_, count_) redone
    uint32_t nextStep_ = 1;
    bool pending_ = false;
    uint32_t lastEdit_ = 0;
};

}  // namespace sidt
