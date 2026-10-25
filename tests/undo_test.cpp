// Undo history: an edit burst is one step, undo / redo swap whole objects,
// a new edit drops the redo side, a full ring forgets the oldest steps
// whole, and the song core comes back without losing its pattern memory.
#include <cstring>
#include <memory>
#include <vector>

#include "app/undo.h"
#include "test.h"

using namespace sidt;

namespace {

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    std::vector<uint8_t> mem = std::vector<uint8_t>(Undo::kBytes);
    Undo u;
    uint32_t ms = 0;
    Box() {
        song->patterns = pats.get();
        song->clear();
        u.init(mem.data(), uint32_t(mem.size()), song.get());
    }
    void edit() { u.touched(ms); }
    void quiet() {
        ms += Undo::kQuietMs;
        u.service(ms);
    }
};

}  // namespace

TEST(no_memory_no_undo) {
    Box b;
    Undo u;
    u.init(b.mem.data(), Undo::kBytes - 1, b.song.get());
    CHECK(!u.ready());
    CHECK(!u.undo());
}

TEST(a_burst_is_one_step) {
    Box b;
    b.song->pattern(3).cells[0][0].note = 10;
    b.edit();
    b.ms += 100;
    b.u.service(b.ms);   // not quiet yet
    CHECK_EQ(b.u.undoSteps(), 0);
    b.song->pattern(5).cells[1][2].note = 20;
    b.song->bpm = 140;
    b.edit();
    b.quiet();
    CHECK_EQ(b.u.undoSteps(), 1);
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(3).cells[0][0].note, 0);
    CHECK_EQ(b.song->pattern(5).cells[1][2].note, 0);
    CHECK_EQ(b.song->bpm, 125);
    CHECK(b.song->patterns == b.pats.get());
    CHECK_EQ(b.u.redoSteps(), 1);
    CHECK(b.u.redo());
    CHECK_EQ(b.song->pattern(3).cells[0][0].note, 10);
    CHECK_EQ(b.song->pattern(5).cells[1][2].note, 20);
    CHECK_EQ(b.song->bpm, 140);
    CHECK(b.song->patterns == b.pats.get());
    CHECK(!b.u.redo());
}

TEST(steps_undo_in_order_and_a_new_edit_drops_redo) {
    Box b;
    for (int i = 1; i <= 3; ++i) {
        b.song->pattern(0).cells[0][0].note = uint8_t(i);
        b.edit();
        b.quiet();
    }
    CHECK_EQ(b.u.undoSteps(), 3);
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(0).cells[0][0].note, 2);
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(0).cells[0][0].note, 1);
    b.song->pattern(0).cells[0][0].note = 9;
    b.edit();
    b.u.service(b.ms, true);   // forced: recorded at once
    CHECK_EQ(b.u.redoSteps(), 0);
    CHECK_EQ(b.u.undoSteps(), 2);
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(0).cells[0][0].note, 1);
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(0).cells[0][0].note, 0);
    CHECK(!b.u.undo());
}

TEST(touch_without_change_records_nothing) {
    Box b;
    b.edit();
    b.quiet();
    CHECK_EQ(b.u.undoSteps(), 0);
}

TEST(a_full_ring_forgets_whole_oldest_steps) {
    Box b;
    // Step 1 changes two patterns; then single-pattern steps fill the ring.
    b.song->pattern(1).cells[0][0].note = 1;
    b.song->pattern(2).cells[0][0].note = 1;
    b.edit();
    b.quiet();
    for (int i = 0; i < Undo::kEntries - 1; ++i) {
        b.song->pattern(7).cells[0][0].note = uint8_t(i + 1);
        b.edit();
        b.quiet();
    }
    // 2 + 159 entries > 160: step 1 is gone entirely, never half undone.
    CHECK_EQ(b.u.undoSteps(), Undo::kEntries - 1);
    while (b.u.undo()) {
    }
    CHECK_EQ(b.song->pattern(7).cells[0][0].note, 0);
    CHECK_EQ(b.song->pattern(1).cells[0][0].note, 1);
    CHECK_EQ(b.song->pattern(2).cells[0][0].note, 1);
    while (b.u.redo()) {
    }
    CHECK_EQ(b.song->pattern(7).cells[0][0].note, Undo::kEntries - 1);
}

TEST(reset_clears_the_history) {
    Box b;
    b.song->pattern(0).cells[0][0].note = 5;
    b.edit();
    b.quiet();
    b.song->clear();   // a load / new song
    b.u.reset();
    CHECK(!b.u.undo());
    b.song->pattern(0).cells[0][0].note = 6;
    b.edit();
    b.quiet();
    CHECK(b.u.undo());
    CHECK_EQ(b.song->pattern(0).cells[0][0].note, 0);
}
