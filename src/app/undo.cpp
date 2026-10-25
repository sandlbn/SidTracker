// SIDTracker -- undo / redo (undo.h).
#include "app/undo.h"

#include <cstring>

namespace sidt {

void Undo::init(uint8_t* mem, uint32_t bytes, Song* song) {
    song_ = song;
    mirror_ = nullptr;
    if (!mem || bytes < kBytes || !song) return;
    mirror_ = reinterpret_cast<Song*>(mem);
    mirrorPats_ = reinterpret_cast<Pattern*>(mem + sizeof(Song));
    ring_ = mem + sizeof(Song) + sizeof(Pattern) * kPatternCount;
    reset();
}

void Undo::reset() {
    if (!mirror_) return;
    std::memcpy(static_cast<void*>(mirror_), song_, sizeof(Song));
    std::memcpy(static_cast<void*>(mirrorPats_), song_->patterns, sizeof(Pattern) * kPatternCount);
    first_ = count_ = cursor_ = 0;
    pending_ = false;
}

void Undo::touched(uint32_t nowMs) {
    pending_ = true;
    lastEdit_ = nowMs;
}

void Undo::service(uint32_t nowMs, bool force) {
    if (!mirror_ || !pending_) return;
    if (!force && nowMs - lastEdit_ < kQuietMs) return;
    pending_ = false;
    record();
}

void Undo::record() {
    // Objects that differ from the mirror: their old copies form one step.
    int changed[1 + kPatternCount];
    int n = 0;
    if (std::memcmp(song_, mirror_, sizeof(Song))) changed[n++] = -1;
    for (int p = 0; p < kPatternCount; ++p)
        if (std::memcmp(&song_->patterns[p], &mirrorPats_[p], sizeof(Pattern))) changed[n++] = p;
    if (!n) return;
    if (n > kEntries) n = kEntries;   // (a whole-song change larger than the history: keep what fits)
    count_ = cursor_;                 // a new edit drops what could be redone
    const uint32_t step = nextStep_++;
    for (int i = 0; i < n; ++i) {
        if (count_ == kEntries) {     // full: forget the oldest step entirely
            const uint32_t oldest = entries_[first_].step;
            while (count_ && entries_[first_].step == oldest) {
                first_ = (first_ + 1) % kEntries;
                --count_;
            }
        }
        const int ring = ringIndex(count_);
        entries_[ring] = {step, int16_t(changed[i])};
        if (changed[i] < 0) {
            std::memcpy(slot(ring), static_cast<void*>(mirror_), sizeof(Song));
            std::memcpy(static_cast<void*>(mirror_), song_, sizeof(Song));
        } else {
            Pattern& live = song_->patterns[changed[i]];
            Pattern& mir = mirrorPats_[changed[i]];
            std::memcpy(slot(ring), &mir, sizeof(Pattern));
            std::memcpy(&mir, &live, sizeof(Pattern));
        }
        ++count_;
    }
    cursor_ = count_;
}

void Undo::swapIn(int ring) {
    // The entry and the live object trade places (through the spare slot);
    // the mirror follows the live one.
    const Entry& e = entries_[ring];
    uint8_t* tmp = slot(kEntries);
    if (e.object < 0) {
        Pattern* keep = song_->patterns;   // the live song keeps its pattern memory
        std::memcpy(tmp, static_cast<void*>(song_), sizeof(Song));
        std::memcpy(static_cast<void*>(song_), slot(ring), sizeof(Song));
        song_->patterns = keep;
        std::memcpy(slot(ring), tmp, sizeof(Song));
        std::memcpy(static_cast<void*>(mirror_), song_, sizeof(Song));
    } else {
        Pattern& live = song_->patterns[e.object];
        std::memcpy(tmp, &live, sizeof(Pattern));
        std::memcpy(&live, slot(ring), sizeof(Pattern));
        std::memcpy(slot(ring), tmp, sizeof(Pattern));
        std::memcpy(&mirrorPats_[e.object], &live, sizeof(Pattern));
    }
}

bool Undo::undo() {
    if (!mirror_ || !cursor_) return false;
    const uint32_t step = entries_[ringIndex(cursor_ - 1)].step;
    while (cursor_ && entries_[ringIndex(cursor_ - 1)].step == step) {
        --cursor_;
        swapIn(ringIndex(cursor_));
    }
    return true;
}

bool Undo::redo() {
    if (!mirror_ || cursor_ >= count_) return false;
    const uint32_t step = entries_[ringIndex(cursor_)].step;
    while (cursor_ < count_ && entries_[ringIndex(cursor_)].step == step) {
        swapIn(ringIndex(cursor_));
        ++cursor_;
    }
    return true;
}

int Undo::undoSteps() const {
    int n = 0;
    for (int k = 0; k < cursor_; ++k)
        if (k == 0 || entries_[ringIndex(k)].step != entries_[ringIndex(k - 1)].step) ++n;
    return n;
}

int Undo::redoSteps() const {
    int n = 0;
    for (int k = cursor_; k < count_; ++k)
        if (k == cursor_ || entries_[ringIndex(k)].step != entries_[ringIndex(k - 1)].step) ++n;
    return n;
}

}  // namespace sidt
