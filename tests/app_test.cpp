// App + UI host tests over fake HALs: editing from the panel controls,
// project save / load through IStorage, transport.
#include <cstring>  // mem_storage.h (vendored) needs it
#include <deque>
#include <map>
#include <set>
#include <string>
#include <memory>

#include "app/app.h"
#include "mem_storage.h"
#include "test.h"
#include "ui/palette.h"
#include "tracker/song/designer.h"
#include "tracker/song/sound_library.h"
#include "ui/buttons.h"

using namespace sidt;
namespace b = ot::btn;

namespace {

// Display that accepts and drops every command.
struct NullDisplay final : ot::IDisplay {
    uint32_t swaps = 0, texts = 0;
    bool readback = false;
    bool readPixels(uint16_t, uint16_t y, uint16_t w, uint16_t h, uint32_t* rgb) override {
        if (!readback) return false;
        for (uint32_t i = 0; i < uint32_t(w) * h; ++i) rgb[i] = 0x102030u + y;
        return true;
    }
    uint16_t width() const override { return 800; }
    uint16_t height() const override { return 480; }
    void dlStart() override {}
    void clearColorRGB(uint8_t, uint8_t, uint8_t) override {}
    void clear(bool, bool, bool) override {}
    void colorRGB(uint8_t, uint8_t, uint8_t) override {}
    void colorA(uint8_t) override {}
    void lineWidth(uint16_t) override {}
    void pointSize(uint16_t) override {}
    void scissorXY(uint16_t, uint16_t) override {}
    void scissorSize(uint16_t, uint16_t) override {}
    void begin(Prim) override {}
    void end() override {}
    void vertex2f(int32_t, int32_t) override {}
    void vertex2ii(uint16_t, uint16_t, uint8_t, uint8_t) override {}
    void text(int16_t, int16_t, uint8_t, uint16_t, const char*) override { ++texts; }
    void number(int16_t, int16_t, uint8_t, uint16_t, int32_t) override {}
    void swap() override { ++swaps; }
};

// Display that estimates the FT81x display-list words of each frame.
// CMD_TEXT is costed conservatively: 4 words of setup + one VERTEX2II per
// visible glyph + 2 when x >= 512 (translate); every other call is 1 word.
struct CountingDisplay final : ot::IDisplay {
    uint32_t words = 0, maxWords = 0;
    uint16_t width() const override { return 800; }
    uint16_t height() const override { return 480; }
    void dlStart() override { words = 0; }
    void clearColorRGB(uint8_t, uint8_t, uint8_t) override { ++words; }
    void clear(bool, bool, bool) override { ++words; }
    void colorRGB(uint8_t, uint8_t, uint8_t) override { ++words; }
    void colorA(uint8_t) override { ++words; }
    void lineWidth(uint16_t) override { ++words; }
    void pointSize(uint16_t) override { ++words; }
    void scissorXY(uint16_t, uint16_t) override { ++words; }
    void scissorSize(uint16_t, uint16_t) override { ++words; }
    void begin(Prim) override { ++words; }
    void end() override { ++words; }
    void vertex2f(int32_t, int32_t) override { ++words; }
    void vertex2ii(uint16_t, uint16_t, uint8_t, uint8_t) override { ++words; }
    void text(int16_t x, int16_t, uint8_t, uint16_t, const char* s) override {
        // Glyphs at x >= 512 cannot use VERTEX2II: two words each (UX plan
        // P0.4 measured this against the driver).
        words += 4 + (x >= 512 ? 2 : 0);
        for (; *s; ++s)
            if (*s != ' ') words += x >= 512 ? 2 : 1;
    }
    void number(int16_t, int16_t, uint8_t, uint16_t, int32_t) override { words += 10; }
    void swap() override {
        ++words;
        if (words > maxWords) maxWords = words;
    }
};

struct FakeInput final : ot::IInput {
    std::deque<ot::InputEvent> q;
    bool poll(ot::InputEvent& ev) override {
        if (q.empty()) return false;
        ev = q.front();
        q.pop_front();
        return true;
    }
};

struct FakeMidi final : ot::IMidiPort {
    std::vector<uint8_t> out[2];
    std::vector<uint8_t> in[2];
    bool send(ot::MidiPortId port, const uint8_t* d, uint32_t n) override {
        out[int(port)].insert(out[int(port)].end(), d, d + n);
        return true;
    }
    uint32_t receive(ot::MidiPortId port, uint8_t* dst, uint32_t cap) override {
        auto& q = in[int(port)];
        const uint32_t n = uint32_t(q.size()) < cap ? uint32_t(q.size()) : cap;
        for (uint32_t i = 0; i < n; ++i) dst[i] = q[i];
        q.erase(q.begin(), q.begin() + n);
        return n;
    }
};

// MemStorage (vendored) lists files only; the card also lists folders.
struct DirStorage : MemStorage {
    int32_t listDir(const char* path, DirFn fn, void* ctx) override {
        const std::string prefix = std::string(path) + "/";
        std::set<std::string> subs;
        auto note = [&](const std::string& p) {
            if (p.compare(0, prefix.size(), prefix) != 0) return;
            const std::string rest = p.substr(prefix.size());
            const size_t slash = rest.find('/');
            if (slash != std::string::npos) subs.insert(rest.substr(0, slash));
            else if (dirs.count(p)) subs.insert(rest);
        };
        for (auto& kv : files) note(kv.first);
        for (auto& d : dirs) note(d);
        int32_t n = 0;
        for (auto& s : subs) {
            DirEntry e{s.c_str(), true, 0};
            ++n;
            if (!fn(e, ctx)) return n;
        }
        return n + MemStorage::listDir(path, fn, ctx);
    }
};

struct FakeClock final : ot::IClock {
    uint32_t ms = 0;
    uint32_t micros() const override { return ms * 1000; }
    uint32_t millis() const override { return ms; }
};

struct Rig {
    NullDisplay display;
    FakeInput input;
    FakeClock clock;
    DirStorage storage;
    FakeMidi midi;
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<App> app{new App};
    std::vector<uint8_t> scratch = std::vector<uint8_t>(512 * 1024);
    std::vector<int16_t> fx = std::vector<int16_t>(2 * 88200);
    std::vector<int16_t> kit = std::vector<int16_t>(512 * 1024);
    std::vector<uint8_t> c64 = std::vector<uint8_t>(App::kC64MemoryBytes);
    std::vector<uint8_t> undo = std::vector<uint8_t>(Undo::kBytes);
    std::vector<uint8_t> list = std::vector<uint8_t>(App::kInstListBytes);
    Rig() {
        Platform p;
        p.display = &display;
        p.input = &input;
        p.clock = &clock;
        p.storage = &storage;
        p.patterns = pats.get();
        p.scratch = scratch.data();
        p.scratchBytes = uint32_t(scratch.size());
        p.fxMemory = fx.data();
        p.fxFrames = uint32_t(fx.size() / 2);
        p.kitMemory = kit.data();
        p.kitSamples = uint32_t(kit.size());
        p.c64Memory = c64.data();
        p.undoMemory = undo.data();
        p.undoBytes = uint32_t(undo.size());
        p.listMemory = list.data();
        p.listBytes = uint32_t(list.size());
        p.midi = &midi;
        CHECK(app->init(p));
        app->boot();
    }
    void loop(int ms = 20) {
        for (int i = 0; i < ms; ++i) {
            ++clock.ms;
            app->loop();
        }
    }
    void push(ot::InputEvent::Type t, uint8_t id, uint8_t vel = 0, int8_t d = 0) {
        ot::InputEvent e;
        e.type = t;
        e.id = id;
        e.velocity = vel;
        e.delta = d;
        input.q.push_back(e);
    }
    void tap(uint8_t id) {
        push(ot::InputEvent::kButtonDown, id);
        push(ot::InputEvent::kButtonUp, id);
        loop();
    }
    void pad(int p) {
        push(ot::InputEvent::kPadDown, uint8_t(p), 100);
        push(ot::InputEvent::kPadUp, uint8_t(p));
        loop();
    }
    void enc(int d) {
        push(ot::InputEvent::kEncoder, 0, 0, int8_t(d));
        loop();
    }
    // Instrument screen page n (1 GENERAL .. 7 SOUNDS): Shift+F-key.
    void page(int n) {
        push(ot::InputEvent::kButtonDown, b::Shift);
        tap(uint8_t(b::F1 + n - 1));
        push(ot::InputEvent::kButtonUp, b::Shift);
        loop();
    }
};

}  // namespace

TEST(boot_draws_frames) {
    Rig r;
    r.loop(200);
    CHECK(r.display.swaps > 3);
    CHECK(r.display.texts > 100);
}

TEST(pads_enter_notes_only_in_edit_mode) {
    Rig r;
    r.pad(36);   // bottom-left pad = C of octave 3, preview only
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, 0);
    r.tap(b::Rec);
    r.pad(36);
    r.pad(24 + 4);   // E of octave 4
    const Pattern& p = r.app->song.pattern(0);
    CHECK_EQ(p.cells[0][0].note, 1 + 3 * 12);
    CHECK_EQ(p.cells[0][0].inst, 1);
    CHECK_EQ(p.cells[1][0].note, 1 + 4 * 12 + 4);
    CHECK(r.app->dirty());
}

TEST(hex_entry_on_command_field) {
    Rig r;
    r.tap(b::Rec);
    r.tap(b::Fx1);   // command field
    r.pad(0xA);
    r.pad(0x4);
    r.pad(0x2);
    const Cell& c = r.app->song.pattern(0).cells[0][0];
    CHECK_EQ(c.cmd, 0xA);
    CHECK_EQ(c.param, 0x42);
}

TEST(jog_edits_and_delete_clears) {
    Rig r;
    r.tap(b::Rec);
    r.enc(1);   // empty note -> C of the current octave
    Cell& c = r.app->song.pattern(0).cells[0][0];
    CHECK_EQ(c.note, 1 + 3 * 12);
    r.enc(2);
    CHECK_EQ(c.note, 1 + 3 * 12 + 2);
    r.tap(b::Delete);
    CHECK_EQ(c.note, 0);
}

TEST(save_and_load_through_storage) {
    Rig r;
    r.tap(b::Rec);
    r.pad(40);
    r.tap(b::Rec);
    r.tap(b::File);
    r.tap(b::F3);   // save as new: the pad keyboard suggests SONG001
    CHECK(r.app->ui.keyboardOpen());
    r.tap(b::F8);   // OK
    CHECK(r.storage.files.count("SIDTracker/Projects/SONG001.sidt") == 1);
    CHECK(r.storage.files.count("SIDTracker/autosave.sidt") == 1);
    CHECK(!r.app->dirty());
    const uint8_t n = r.app->song.pattern(0).cells[0][0].note;
    CHECK(n != 0);
    r.tap(b::F4);   // new song (not dirty: no confirmation needed)
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, 0);
    r.tap(b::F1);   // load the selected (only) project
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, n);
    CHECK_STR(r.app->fileName(), "SONG001");
}

TEST(autosave_restores_on_boot) {
    MemStorage keep;
    {
        Rig r;
        r.tap(b::Rec);
        r.pad(41);
        r.app->autosave();
        keep.files = r.storage.files;
    }
    Rig r2;
    r2.storage.files = keep.files;
    r2.app->boot();
    CHECK(r2.app->song.pattern(0).cells[0][0].note != 0);
}

TEST(play_and_stop) {
    Rig r;
    r.tap(b::Play);
    // The command reaches the player on the next audio block.
    int16_t buf[256];
    r.app->engine.fill(buf, 128);
    CHECK(r.app->engine.player().playing());
    r.tap(b::Play);
    r.app->engine.fill(buf, 128);
    CHECK(!r.app->engine.player().playing());
}

TEST(song_screen_edits_order) {
    Rig r;
    r.tap(b::Song);
    r.tap(b::Insert);
    CHECK_EQ(r.app->song.orderLen, 2);
    r.enc(3);
    CHECK_EQ(r.app->song.order[1].pattern, 3);
    r.tap(b::Right);
    r.enc(-12);
    CHECK_EQ(r.app->song.order[1].transpose, -12);
    r.tap(b::Delete);
    CHECK_EQ(r.app->song.orderLen, 1);
}

TEST(every_screen_draws) {
    Rig r;
    const uint8_t keys[] = {b::InstrParams, b::Performance, b::Song, b::Config, b::File, b::Master, b::Pattern};
    for (uint8_t k : keys) {
        const uint32_t before = r.display.swaps;
        r.tap(k);
        r.loop(60);
        CHECK(r.display.swaps > before);
    }
    // Instrument pages, including the table pages.
    r.tap(b::InstrParams);
    for (int n = 1; n <= 6; ++n) {
        r.page(n);
        r.enc(1);
    }
}

TEST(display_list_fits_with_a_full_pattern) {
    // Every cell filled: the heaviest Pattern screen frame.
    CountingDisplay d;
    FakeInput in;
    FakeClock clk;
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<App> app(new App);
    Platform p;
    p.display = &d;
    p.input = &in;
    p.clock = &clk;
    p.patterns = pats.get();
    CHECK(app->init(p));
    for (int r = 0; r < kMaxRows; ++r)
        for (int ch = 0; ch < kChannels; ++ch) app->song.pattern(0).cells[r][ch] = {uint8_t(1 + r % 96), 0x3F, 0xA, 0x42};
    for (int i = 0; i < 200; ++i) {
        ++clk.ms;
        app->loop();
    }
    std::printf("  pattern screen, full pattern: %u display-list words (FT81x RAM_DL: 2048)\n", unsigned(d.maxWords));
    CHECK(d.maxWords > 500);
    CHECK(d.maxWords <= 1800);   // 12 % margin for the estimate
    {   // Help over the full pattern (UX plan P0.4: 2,031 words before)
        auto key = [&](uint8_t id) {
            for (int t = 0; t < 2; ++t) {
                ot::InputEvent e;
                e.type = t ? ot::InputEvent::kButtonUp : ot::InputEvent::kButtonDown;
                e.id = id;
                in.q.push_back(e);
            }
        };
        key(b::Help);
        d.maxWords = 0;
        for (int i = 0; i < 100; ++i) {
            ++clk.ms;
            app->loop();
        }
        std::printf("  help over a full pattern: %u words\n", unsigned(d.maxWords));
        CHECK(d.maxWords > 100);
        CHECK(d.maxWords <= 1800);
        key(b::Help);
        for (int i = 0; i < 50; ++i) {
            ++clk.ms;
            app->loop();
        }
        // The instrument picker (replaces the screen while open).
        ot::InputEvent e;
        e.type = ot::InputEvent::kButtonDown;
        e.id = b::Instr;
        in.q.push_back(e);
        d.maxWords = 0;
        for (int i = 0; i < 500; ++i) {
            ++clk.ms;
            app->loop();
        }
        CHECK(app->ui.pickerOpen());
        std::printf("  instrument picker: %u words\n", unsigned(d.maxWords));
        CHECK(d.maxWords <= 1800);
        e.type = ot::InputEvent::kButtonUp;
        in.q.push_back(e);
        for (int i = 0; i < 50; ++i) {
            ++clk.ms;
            app->loop();
        }
    }
    // Every other screen and instrument page, with full tables.
    for (int k = 0; k < kTableKinds; ++k)
        for (int i = 0; i < kTableLen; ++i) {
            app->song.tables[k].left[i] = uint8_t(0x41 + i);
            app->song.tables[k].right[i] = uint8_t(i);
        }
    auto press = [&](uint8_t id) {
        for (int t = 0; t < 2; ++t) {
            ot::InputEvent e;
            e.type = t ? ot::InputEvent::kButtonUp : ot::InputEvent::kButtonDown;
            e.id = id;
            in.q.push_back(e);
        }
        for (int i = 0; i < 100; ++i) {
            ++clk.ms;
            app->loop();
        }
    };
    const uint8_t screens[] = {b::Song, b::Performance, b::Config, b::File, b::Master, b::SampleLoader, b::InstrParams};
    // (Master opens the MASTER mixer; the benchmark page is Shift+Master.)
    for (uint8_t sc : screens) {
        d.maxWords = 0;
        press(sc);
        std::printf("  screen 0x%02X: %u words\n", unsigned(sc), unsigned(d.maxWords));
        CHECK(d.maxWords <= 1800);
    }
    // The Instr. Params key cycles the pages: GENERAL -> DESIGN ... -> SOUNDS.
    for (int pg = 1; pg < 7; ++pg) {
        d.maxWords = 0;
        press(b::InstrParams);
        std::printf("  instrument page %d: %u words\n", pg, unsigned(d.maxWords));
        CHECK(d.maxWords <= 1800);
    }
    // SOUNDS: every preset's program listing.
    d.maxWords = 0;
    press(b::Down);   // preset row
    for (int i = 0; i < 60; ++i) {
        ot::InputEvent e;
        e.type = ot::InputEvent::kEncoder;
        e.delta = 1;
        in.q.push_back(e);
        for (int k = 0; k < 20; ++k) {
            ++clk.ms;
            app->loop();
        }
    }
    std::printf("  sounds page, all presets: %u words\n", unsigned(d.maxWords));
    CHECK(d.maxWords <= 1800);
}


TEST(goattracker_export_then_import_from_the_file_screen) {
    Rig r;
    r.tap(b::Rec);
    r.pad(36);
    r.pad(40);
    r.tap(b::Rec);
    const uint8_t n0 = r.app->song.pattern(0).cells[0][0].note;
    const uint8_t n1 = r.app->song.pattern(0).cells[1][0].note;
    r.tap(b::File);
    r.tap(b::F5);   // GoatTracker mode
    r.tap(b::F2);   // export check + dialog
    CHECK(r.app->ui.dialogOpen());
    CHECK(r.storage.files.count("SIDTracker/GoatTracker/UNTITLED.sng") == 0);
    r.tap(b::F8);   // EXPORT
    CHECK(r.storage.files.count("SIDTracker/GoatTracker/UNTITLED.sng") == 1);
    const auto& f = r.storage.files["SIDTracker/GoatTracker/UNTITLED.sng"];
    CHECK(f.size() > 100 && std::memcmp(f.data(), "GTS5", 4) == 0);
    r.tap(b::F3);   // export instrument 1
    CHECK(r.storage.files.count("SIDTracker/GoatTracker/PULSE_LEAD.ins") == 1);
    r.app->newSong();
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, 0);
    // The list is sorted: PULSE_LEAD.ins, UNTITLED.sng.
    r.tap(b::Down);
    r.tap(b::F1);   // import the song
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, n0);
    CHECK_EQ(r.app->song.pattern(0).cells[1][0].note, n1);
    CHECK(r.app->goatReport().ok);
    // The instrument import goes into the current slot with relocated tables.
    r.app->ui.inst = 9;
    r.tap(b::Up);
    r.tap(b::F1);
    CHECK_STR(r.app->song.inst[9].name, "PULSE LEAD");
    CHECK(r.app->song.inst[9].wavePtr > 1);
}

TEST(perform_automation_recording) {
    Rig r;
    int16_t audio[256];
    auto run = [&](int ms) {   // UI loop plus audio blocks (about 3 ms each)
        for (int i = 0; i < ms; i += 3) {
            r.app->engine.fill(audio, 128);
            r.loop(3);
        }
    };
    r.tap(b::Rec);
    r.pad(36);   // a note on track 1 so the song plays something
    r.tap(b::Performance);
    r.tap(b::Play);
    run(30);
    CHECK(r.app->engine.player().playing());
    // SID1 cutoff row, CUT control: turn while playing with Rec on.
    for (int i = 0; i < 6; ++i) r.tap(b::Down);
    CHECK_EQ(r.app->song.automatedLanes(), 0);
    r.enc(3);
    CHECK(r.app->song.automatedLanes() & (1u << kAutoCutoff));
    // Keep turning across rows: every new row gets the value (latch).
    for (int i = 0; i < 10; ++i) {
        r.enc(1);
        run(100);
    }
    const Pattern& p = r.app->song.pattern(0);
    int rows = 0;
    for (int row = 0; row < p.rows; ++row) rows += (p.autoSet[row] >> kAutoCutoff) & 1;
    CHECK(rows >= 5);
    // F2 + F8 (dialog) clears the lanes of the selected row in this pattern.
    r.tap(b::F2);
    r.tap(b::F8);
    CHECK_EQ(r.app->song.pattern(0).autoLanes(), 0);
    CHECK(r.app->dirty());
}

TEST(drum_column_and_kit_wavs) {
    // A tiny 16-bit mono WAV in slot 9 on the card.
    std::vector<uint8_t> wav = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                                1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xAC, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a',
                                200, 0, 0, 0};
    for (int i = 0; i < 100; ++i) {
        wav.push_back(uint8_t(i * 300));
        wav.push_back(uint8_t((i * 300) >> 8));
    }
    Rig r;
    r.storage.files["SIDTracker/Kit/09_BLIP.wav"] = wav;
    CHECK_EQ(r.app->loadKit(), 1);
    CHECK_STR(r.app->engine.kit().slot(9).name, "BLIP");
    CHECK_EQ(r.app->engine.kit().slot(9).rateHz, 22050u);
    CHECK_STR(r.app->engine.kit().slot(1).name, "KICK");
    // Pattern screen: move to the drum column (Shift+Left from track 1 wraps).
    r.tap(b::Rec);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Left);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    r.pad(8);   // slot 9
    CHECK_EQ(r.app->song.pattern(0).drum[0], 9);
    r.enc(-1);  // the cursor advanced one row: row 1 gets slot 0 -> stays 0
    CHECK_EQ(r.app->song.pattern(0).drum[1], 0);
    r.tap(b::Up);
    r.tap(b::Delete);
    CHECK_EQ(r.app->song.pattern(0).drum[0], 0);
    // Extended screen: effects page edits the song.
    r.tap(b::SampleLoader);
    r.enc(20);
    CHECK_EQ(r.app->song.fx.delayMix, 20);
    r.tap(b::F8);
    CHECK(!r.app->engine.effects());
}

TEST(file_page_exports_sid) {
    Rig r;
    r.app->song.pattern(0).cells[0][0] = {37, 1, 0, 0};
    r.tap(b::File);
    r.tap(b::F6);
    r.tap(b::F7);   // CANCEL
    CHECK(!r.app->ui.dialogOpen());
    CHECK(r.storage.files.empty() || r.storage.files.count("SIDTracker/SID/UNTITLED.sid") == 0);
    r.tap(b::F6);
    r.tap(b::F8);   // EXPORT
    bool found = false;
    for (auto& f : r.storage.files)
        if (f.first.rfind("SIDTracker/SID/", 0) == 0 && f.first.size() > 4 &&
            f.first.compare(f.first.size() - 4, 4, ".sid") == 0) {
            found = true;
            CHECK(f.second.size() > 0x7E);
            CHECK(f.second[0] == 'P' && f.second[1] == 'S');
        }
    CHECK(found);
}

TEST(sounds_page_loads_presets_and_writes_arps) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);   // instrument 2 (empty)
    CHECK_EQ(r.app->ui.inst, 2);
    r.page(7);      // SOUNDS
    r.tap(b::Down); // preset row
    r.enc(2);       // third bass preset: tried at once
    CHECK_STR(r.app->song.inst[2].name, sidt::library::preset(2).name);
    CHECK(r.app->song.inst[2].wavePtr != 0);
    r.tap(b::EncPush);   // keep it
    CHECK_STR(r.app->song.inst[2].name, sidt::library::preset(2).name);
    // UX plan P1.3: trying presets and CANCEL restores the slot and tables exactly.
    const Instrument before = r.app->song.inst[2];
    Table tablesBefore[kTableKinds];
    std::memcpy(tablesBefore, r.app->song.tables, sizeof tablesBefore);
    for (int i = 0; i < 30; ++i) r.enc(1);   // many tries: the tables must not fill
    CHECK(std::strcmp(r.app->song.inst[2].name, before.name) != 0);
    r.tap(b::F7);   // CANCEL
    CHECK_STR(r.app->song.inst[2].name, before.name);
    CHECK(!std::memcmp(&r.app->song.inst[2], &before, sizeof before));
    CHECK(!std::memcmp(r.app->song.tables, tablesBefore, sizeof tablesBefore));
    r.enc(1);
    char tried[kNameLen + 1];
    std::memcpy(tried, r.app->song.inst[2].name, sizeof tried);
    CHECK(std::strcmp(tried, before.name) != 0);
    r.tap(b::F8);   // KEEP
    CHECK_STR(r.app->song.inst[2].name, tried);
    CHECK_EQ(r.app->ui.inst, 2);   // F8 kept, it did not move to the next instrument
    // Arpeggiator: chord row, minor, write.
    r.tap(b::Down);
    r.enc(1);
    const uint8_t wpBefore = r.app->song.inst[2].wavePtr;
    r.tap(b::EncPush);
    const uint8_t ptr = r.app->song.inst[2].wavePtr;
    CHECK(ptr != wpBefore);
    CHECK_EQ(r.app->song.tables[kWaveTable].r(ptr + 1), 3);   // minor third
    CHECK(r.app->dirty());
    // F7: previous instrument.
    r.tap(b::F7);
    CHECK_EQ(r.app->ui.inst, 1);
}

TEST(theme_and_volume_are_saved_and_restored) {
    std::map<std::string, std::vector<uint8_t>> card;
    {
        Rig r;
        r.tap(b::Config);
        for (int i = 0; i < 6; ++i) r.tap(b::Down);   // THEME row
        r.enc(1);
        CHECK_EQ(r.app->settings().theme, 1);
        CHECK_EQ(sidt::ui::col::kBg, sidt::ui::theme(1).bg);
        r.loop(1200);   // written 1 s after the change
        CHECK(r.storage.files.count("SIDTracker/settings.txt") == 1);
        card = r.storage.files;
    }
    sidt::ui::applyTheme(0);
    Rig r2;
    r2.storage.files = card;
    r2.app->boot();
    CHECK_EQ(r2.app->settings().theme, 1);
    CHECK_EQ(sidt::ui::col::kBg, sidt::ui::theme(1).bg);
    sidt::ui::applyTheme(0);   // other tests expect the default theme
}

TEST(shift_power_saves_a_screenshot) {
    Rig r;
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Power);
    CHECK(r.storage.files.count("SIDTracker/Screenshots/SHOT0001.bmp") == 0);   // no readback: no file
    r.display.readback = true;
    r.tap(b::Power);
    r.tap(b::Power);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK(r.storage.files.count("SIDTracker/Screenshots/SHOT0001.bmp") == 1);
    CHECK(r.storage.files.count("SIDTracker/Screenshots/SHOT0002.bmp") == 1);
    const auto& f = r.storage.files["SIDTracker/Screenshots/SHOT0001.bmp"];
    CHECK_EQ(f.size(), 54u + 800u * 480u * 3u);
    CHECK(f[0] == 'B' && f[1] == 'M');
    CHECK_EQ(f[54], 0x30);   // first pixel B G R of 0x102030
    CHECK_EQ(f[56], 0x10);
    // Power without Shift is the HAL's (power off): the UI ignores it.
    r.tap(b::Power);
    CHECK(r.storage.files.count("SIDTracker/Screenshots/SHOT0003.bmp") == 0);
}

TEST(master_page_mixes_and_opens_the_benchmark) {
    Rig r;
    r.tap(b::Master);
    CHECK(r.app->ui.current() == sidt::ui::ScreenId::kMaster);
    // Page 1 (mixer): hold F2 + jog = track 2 level; a tap mutes.
    r.push(ot::InputEvent::kButtonDown, b::F2);
    r.enc(-4);
    r.push(ot::InputEvent::kButtonUp, b::F2);
    r.loop();
    CHECK_EQ(r.app->song.mix.volume[1], 80);
    CHECK(!r.app->engine.muted(1));   // turning the jog is not a tap
    CHECK(r.app->song.extendedFeatures() & kExtMixer);
    r.tap(b::F3);
    CHECK(r.app->engine.muted(2));
    r.tap(b::F3);
    CHECK(!r.app->engine.muted(2));
    r.push(ot::InputEvent::kButtonDown, b::F8);
    r.enc(-2);   // master volume 80 -> 70
    r.push(ot::InputEvent::kButtonUp, b::F8);
    r.loop();
    CHECK_EQ(r.app->settings().volume, 70);
    r.tap(b::F2);
    r.tap(b::F2);   // select track 2 (mute on, off)
    r.tap(b::Delete);
    CHECK_EQ(r.app->song.mix.volume[1], 100);
    // Page 2: the settings list.
    r.tap(b::Master);
    r.tap(b::Down);
    r.tap(b::Down);
    r.enc(-4);   // drum volume 80 -> 60
    CHECK_EQ(r.app->song.drumVolume, 60);
    CHECK(r.app->engine.effects());
    r.tap(b::F1);
    CHECK(!r.app->engine.effects());
    r.tap(b::F8);
    CHECK(r.app->ui.current() == sidt::ui::ScreenId::kBench);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Pattern);
    r.tap(b::Master);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK(r.app->ui.current() == sidt::ui::ScreenId::kBench);
}

namespace {
const char* gFlashed = nullptr;
bool fakeUpdate(ot::IStorage&, const char* image) {
    gFlashed = image;
    return true;
}
bool fakeName(const char* n) { return std::strncmp(n, "PolyendTrac", 11) == 0; }
}  // namespace

TEST(firmware_update_with_f_keys_only) {
    Rig r;
    r.storage.files["Firmware/PolyendTrackerSTv099.ptf"] = {1, 2, 3};
    Platform p = r.app->platform();
    p.requestFirmwareUpdate = &fakeUpdate;
    p.isUpdateImageName = &fakeName;
    // Re-init with the update hooks (as on the device).
    r.app.reset(new App);
    CHECK(r.app->init(p));
    gFlashed = nullptr;
    r.tap(b::Config);
    r.tap(b::F7);   // FIRMWARE row
    r.tap(b::F8);   // INSTALL
    CHECK(gFlashed == nullptr);
    r.tap(b::F8);   // CONFIRM
    CHECK(gFlashed != nullptr);
    if (gFlashed) CHECK_STR(gFlashed, "PolyendTrackerSTv099.ptf");
}

TEST(midi_keyboard_enters_notes_and_cc_moves_the_cutoff) {
    Rig r;
    r.tap(b::Rec);   // pattern, edit mode, note field
    r.midi.in[0] = {0x90, 60, 100, 0x80, 60, 0, 0x90, 64, 100, 0x80, 64, 0};   // C-4, E-4 on DIN
    r.loop();
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, 49);   // C-4
    CHECK_EQ(r.app->song.pattern(0).cells[1][0].note, 53);   // E-4
    // Channel filter: only channel 2 now.
    r.app->setMidi(2, 0, 0, 0);
    r.midi.in[1] = {0x90, 67, 100};   // channel 1 on USB: ignored
    r.loop();
    CHECK_EQ(r.app->song.pattern(0).cells[2][0].note, 0);
    r.midi.in[1] = {0xB1, 74, 127};   // CC 74 on channel 2: cutoff up
    r.loop();
    CHECK(r.app->engine.player().perform.cutoffOffset[0] > 1000);
}

TEST(midi_clock_in_sets_the_tempo) {
    Rig r;
    r.app->setMidi(0, 0, 1, 0);   // sync from DIN
    // 120 bpm = 48 clocks a second: one every 20.8 ms.
    for (int i = 0; i < 60; ++i) {
        r.midi.in[0].push_back(0xF8);
        r.loop(i % 6 == 0 ? 20 : 21);
    }
    CHECK(r.app->midiSynced());
    const uint32_t bpm = r.app->midiSyncBpmX10();
    CHECK(bpm > 1150 && bpm < 1250);
    // Start: the song plays.
    r.midi.in[0].push_back(0xFA);
    r.loop();
    r.midi.in[0].push_back(0xFC);
    r.loop();
    // Clock gone for a second: back to the song's own tempo.
    r.loop(1000);
    CHECK(!r.app->midiSynced());
}

TEST(midi_settings_saved) {
    Rig r;
    r.app->setMidi(5, 3, 2, 2);
    r.loop(1200);
    const auto& f = r.storage.files["SIDTracker/settings.txt"];
    const std::string s(f.begin(), f.end());
    CHECK(s.find("midi_channel=5") != std::string::npos);
    CHECK(s.find("asid_out=2") != std::string::npos);
}

TEST(shift_rec_undoes_and_shift_sample_rec_redoes) {
    Rig r;
    r.tap(b::Rec);
    r.pad(36);
    r.pad(37);   // one burst: two notes
    r.loop(400);
    r.pad(38);   // a second step
    // Undo right away: the pending burst is recorded first.
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Rec);
    const Pattern& p = r.app->song.pattern(0);
    CHECK_EQ(p.cells[2][0].note, 0);
    CHECK_EQ(p.cells[1][0].note, 1 + 3 * 12 + 1);
    CHECK(r.app->ui.edit);   // Shift+Rec does not toggle edit mode
    r.tap(b::Rec);
    CHECK_EQ(p.cells[0][0].note, 0);
    CHECK_EQ(p.cells[1][0].note, 0);
    CHECK_EQ(r.app->undoSteps(), 0);
    CHECK(!r.app->undo());
    r.tap(b::SampleRecorder);
    CHECK_EQ(p.cells[1][0].note, 1 + 3 * 12 + 1);
    CHECK_EQ(p.cells[2][0].note, 0);
    r.tap(b::SampleRecorder);
    CHECK_EQ(p.cells[2][0].note, 1 + 3 * 12 + 2);
    CHECK_EQ(r.app->redoSteps(), 0);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    // A new song clears the history.
    r.app->newSong();
    CHECK_EQ(r.app->undoSteps(), 0);
    CHECK(!r.app->undo());
}

TEST(song_screen_multispeed_changes_only_the_call_rate) {
    // As GoatTracker -S: speed counts calls and stays (a GT 2x song plays at x2 as imported).
    Rig r;
    r.tap(b::Song);
    r.tap(b::F8);
    CHECK_EQ(r.app->song.multi, 2);
    CHECK_EQ(r.app->song.speed, 6);
    r.tap(b::F8);
    CHECK_EQ(r.app->song.multi, 3);
    CHECK_EQ(r.app->song.speed, 6);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::F8);
    r.tap(b::F8);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK_EQ(r.app->song.multi, 1);
    CHECK_EQ(r.app->song.speed, 6);
    CHECK(r.app->dirty());
}

// Fable review 2026-10-22: the DESIGN page kept its cached design after an
// undo, so the next knob turn re-applied the undone change.
TEST(undo_on_the_design_page_survives_the_next_knob_turn) {
    Rig r;
    r.tap(b::InstrParams);
    r.page(2);
    r.tap(b::Down);
    r.tap(b::Down);   // FILTER page, FILTER column
    r.enc(1);
    r.loop(400);
    r.tap(b::F2);     // CUTOFF column
    const uint8_t before = designer::decode(r.app->song, 1).cutoff;
    r.enc(10);
    r.loop(400);
    CHECK(designer::decode(r.app->song, 1).cutoff != before);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Rec);   // undo the cutoff turn
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK_EQ(designer::decode(r.app->song, 1).cutoff, before);
    r.tap(b::Right);  // RESONANCE
    r.enc(1);
    r.loop(400);
    CHECK_EQ(designer::decode(r.app->song, 1).cutoff, before);
}

// Fable review 2026-10-22: the Perform balance row changed the song without
// markDirty, so a later undo of something else reverted it too.
TEST(perform_balance_is_an_edit_of_its_own) {
    Rig r;
    r.tap(b::Performance);
    r.tap(b::Up);   // balance row
    const uint8_t bal = r.app->song.sid.balance;
    r.enc(2);
    CHECK(r.app->dirty());
    r.loop(400);
    r.tap(b::Pattern);
    r.tap(b::Rec);
    r.pad(36);
    r.loop(400);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Rec);   // undo the note only
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].note, 0);
    CHECK(r.app->song.sid.balance != bal);
}

// Fable review 2026-10-22: a pattern shortened under the cursor (undo) must
// not take a note below its end.
TEST(pattern_cursor_clamps_when_the_length_shrinks) {
    Rig r;
    r.app->song.pattern(0).rows = 64;
    r.tap(b::Rec);
    for (int i = 0; i < 3; ++i) {
        r.push(ot::InputEvent::kButtonDown, b::Shift);
        r.tap(b::Down);
        r.push(ot::InputEvent::kButtonUp, b::Shift);
    }
    r.app->song.pattern(0).rows = 16;   // as an undo would
    r.push(ot::InputEvent::kPadDown, 36, 100);   // in the same pass, before a redraw
    r.push(ot::InputEvent::kPadUp, 36);
    r.app->loop();
    CHECK_EQ(r.app->song.pattern(0).cells[48][0].note, 0);
    CHECK_EQ(r.app->song.pattern(0).cells[15][0].note, 1 + 3 * 12);
}

TEST(general_page_edits_the_envelope_and_delete_twice_inits) {
    // UX plan P0.1 / P0.2.
    Rig r;
    Instrument& in = r.app->song.inst[1];   // PULSE LEAD, with tables
    const uint8_t wp = in.wavePtr;
    r.tap(b::InstrParams);
    r.page(1);
    r.tap(b::Down);   // ATTACK
    r.enc(5);
    CHECK_EQ(in.ad >> 4, 5);
    r.tap(b::Down);
    r.tap(b::Down);   // SUSTAIN
    r.enc(-3);
    CHECK_EQ(in.sr >> 4, 0xA - 3);
    CHECK_EQ(in.wavePtr, wp);   // the tables are not touched
    r.tap(b::Delete);
    CHECK(!in.empty());         // Delete asks
    CHECK(r.app->ui.dialogOpen());
    r.enc(3);                   // the dialog takes the jog too
    CHECK_EQ(in.ad >> 4, 5);
    r.tap(b::F8);               // INIT
    CHECK(in.empty());
    CHECK_STR(in.name, "INSTR 01");
}

TEST(hold_instr_opens_the_picker_and_pads_choose) {
    // UX plan P1.1.
    Rig r;
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(400);   // held past 333 ms
    CHECK(r.app->ui.pickerOpen());
    r.pad(5);      // slot 06, played
    CHECK_EQ(r.app->ui.inst, 1);   // not chosen until release
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK(!r.app->ui.pickerOpen());
    CHECK_EQ(r.app->ui.inst, 6);
    // A short tap on the Pattern screen only selects the instrument column.
    r.tap(b::Instr);
    CHECK(!r.app->ui.pickerOpen());
    // Edit mode: Insert writes the picked slot into the cell.
    r.tap(b::Rec);
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(400);
    r.enc(3);      // 06 -> 09
    r.tap(b::Insert);
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK_EQ(r.app->song.pattern(0).cells[0][0].inst, 9);
    CHECK_EQ(r.app->ui.inst, 9);
    // Instrument screen: a tap opens it, Instr again chooses, F8 = page 2.
    r.tap(b::InstrParams);
    r.tap(b::Instr);
    CHECK(r.app->ui.pickerOpen());
    r.tap(b::F8);
    r.enc(1);
    r.tap(b::Instr);
    CHECK(!r.app->ui.pickerOpen());
    CHECK_EQ(r.app->ui.inst, 9 + 48 + 1);
}

TEST(shift_left_right_on_the_instrument_screen_moves_the_audition_track) {
    // UX plan P1.2.
    Rig r;
    r.tap(b::InstrParams);
    CHECK_EQ(r.app->ui.channel, 0);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Right);
    r.tap(b::Right);
    r.tap(b::Left);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK_EQ(r.app->ui.channel, 1);
}

TEST(presets_load_with_a_short_preview) {
    // UX plan P1.6.
    Rig r;
    int16_t buf[256];
    auto render = [&]() {
        for (int i = 0; i < 4; ++i) r.app->engine.fill(buf, 128);
    };
    r.tap(b::InstrParams);
    r.tap(b::F8);
    r.page(7);
    r.tap(b::Down);
    r.tap(b::Insert);
    CHECK_STR(r.app->song.inst[2].name, sidt::library::preset(0).name);
    render();
    CHECK(r.app->engine.chip(0).shadow(4) & 0x01);    // gate on: the preview plays
    r.loop(400);
    render();
    CHECK(!(r.app->engine.chip(0).shadow(4) & 0x01));  // released after 300 ms
}

TEST(instrument_bar_pages_and_table_actions) {
    // UX plan P1.4: the module key cycles pages, F-keys are actions.
    Rig r;
    r.tap(b::InstrParams);   // GENERAL
    r.tap(b::InstrParams);   // DESIGN
    r.tap(b::InstrParams);   // WAVE table page
    Song& s = r.app->song;
    const uint8_t w0 = s.tables[kWaveTable].left[0];
    r.tap(b::F1);            // INS ROW at row 01: the PULSE LEAD program moves down
    CHECK_EQ(s.tables[kWaveTable].left[1], w0);
    CHECK_EQ(s.inst[1].wavePtr, 2);
    r.tap(b::F2);            // DEL ROW: back
    CHECK_EQ(s.tables[kWaveTable].left[0], w0);
    CHECK_EQ(s.inst[1].wavePtr, 1);
    r.tap(b::F8);            // NEXT INS
    CHECK_EQ(r.app->ui.inst, 2);
    r.tap(b::F3);            // POINT HERE
    CHECK_EQ(s.inst[2].wavePtr, 1);
    // GENERAL: COPY on 1, PASTE onto 3.
    r.page(1);
    r.tap(b::F7);
    r.tap(b::F2);            // COPY instrument 1
    r.tap(b::F8);
    r.tap(b::F8);            // instrument 3 (init: no question)
    r.tap(b::F3);            // PASTE
    CHECK_STR(s.inst[3].name, s.inst[1].name);
    CHECK_EQ(s.inst[3].pulsePtr, s.inst[1].pulsePtr);
    // Shift+Instr. Params goes back a page.
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::InstrParams);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    r.tap(b::F1);            // on SOUNDS: no action, no crash
    CHECK_EQ(r.app->ui.inst, 3);
}

TEST(shift_f_keys_on_the_pattern_screen_mute_tracks) {
    // UX plan P2.3c: the stock Tracker's mute page.
    Rig r;
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::F2);
    r.tap(b::F7);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK(r.app->engine.muted(1));
    CHECK(r.app->engine.muted(6));   // drums
    CHECK_EQ(r.app->ui.pattern, 0);  // F2 without Shift would have gone to pattern 01
    r.tap(b::F2);
    CHECK_EQ(r.app->ui.pattern, 1);
}

TEST(pad_keyboard_names_things) {
    // UX plan P2.4.
    Rig r;
    r.tap(b::File);
    r.tap(b::F3);
    CHECK(r.app->ui.keyboardOpen());
    for (int i = 0; i < 7; ++i) r.tap(b::Delete);   // erase SONG001
    r.pad(12 + 7);   // I (second pad row: QWERTYUIOP)
    r.pad(24 + 3);   // F
    r.pad(36 + 11);  // space: not allowed in a file name
    r.pad(1);        // 2
    r.tap(b::F8);
    CHECK(r.storage.files.count("SIDTracker/Projects/IF2.sidt") == 1);
    CHECK_STR(r.app->fileName(), "IF2");
    // Saving the same name again asks before overwriting.
    r.tap(b::F3);
    for (int i = 0; i < 7; ++i) r.tap(b::Delete);
    r.pad(12 + 7);
    r.pad(24 + 3);
    r.pad(1);
    r.tap(b::F8);    // OK -> "IF2 exists"
    CHECK(r.app->ui.dialogOpen());
    r.tap(b::F8);    // OVERWRITE
    CHECK(!r.app->ui.dialogOpen());
    CHECK(!r.app->ui.keyboardOpen());
    // The instrument name.
    r.tap(b::InstrParams);
    r.tap(b::Insert);   // NAME row
    CHECK(r.app->ui.keyboardOpen());
    for (int i = 0; i < 24; ++i) r.tap(b::Delete);
    r.pad(36);       // Z
    r.pad(36 + 11);  // space allowed here
    r.pad(0);        // 1
    r.tap(b::F8);
    CHECK_STR(r.app->song.inst[1].name, "Z 1");
    // Cancel keeps the old name.
    r.tap(b::Insert);
    r.pad(0);
    r.tap(b::F7);
    CHECK_STR(r.app->song.inst[1].name, "Z 1");
}

// --- Fable review 2026-10-22 (UX P1 / P2) ------------------------------------

namespace {
void render(Rig& r) {
    int16_t buf[256];
    for (int i = 0; i < 4; ++i) r.app->engine.fill(buf, 128);
}
bool gate(Rig& r, int ch) { return r.app->engine.chip(ch / 3).shadow(uint8_t((ch % 3) * 7 + 4)) & 1; }
void shiftTap(Rig& r, uint8_t id) {
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(id);
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
}
}  // namespace

TEST(sounds_snapshot_never_lands_in_another_slot_or_song) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);          // slot 2
    r.page(7);
    r.tap(b::Down);
    r.enc(1);              // trying a preset on slot 2
    // Another slot picked: CANCEL there must not write slot 2's snapshot.
    r.tap(b::Instr);       // picker (sticky on this screen)
    r.tap(b::Right);       // slot 3
    r.tap(b::Instr);       // choose
    CHECK_EQ(r.app->ui.inst, 3);
    const Instrument three = r.app->song.inst[3];
    r.tap(b::F7);          // PREV INS (no try on slot 3)
    CHECK(!std::memcmp(&r.app->song.inst[3], &three, sizeof three));
    // A new song while trying: CANCEL afterwards changes nothing.
    r.tap(b::F8);          // back to slot 3
    r.enc(1);              // try on slot 3
    r.app->newSong();
    r.tap(b::InstrParams);
    r.page(7);
    r.tap(b::F7);
    CHECK_STR(r.app->song.inst[1].name, "PULSE LEAD");
}

TEST(pads_are_released_around_dialogs_and_the_picker) {
    Rig r;
    // A pad held while a dialog opens is released when it is let go.
    r.tap(b::InstrParams);
    r.push(ot::InputEvent::kPadDown, 36, 100);
    r.loop();
    r.tap(b::F1);          // INIT? dialog
    CHECK(r.app->ui.dialogOpen());
    r.push(ot::InputEvent::kPadUp, 36);
    r.loop();
    r.tap(b::F7);
    render(r);
    CHECK(!gate(r, 0));
    // A pad pressed inside the picker and released after it closed (Perform).
    r.tap(b::Performance);
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(400);
    r.push(ot::InputEvent::kPadDown, 0, 100);
    r.loop();
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK(!r.app->ui.pickerOpen());
    r.push(ot::InputEvent::kPadUp, 0);
    r.loop();
    render(r);
    CHECK(!gate(r, 0));
}

TEST(picker_page_key_and_drum_column_audition) {
    Rig r;
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(400);
    r.enc(19);             // slot 20
    r.tap(b::F8);          // page 2
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK(r.app->ui.inst > 48);
    // On the drum column the picker still auditions (on track 1).
    for (int i = 0; i < 6; ++i) shiftTap(r, b::Right);
    CHECK_EQ(r.app->ui.channel, 6);
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(400);
    r.push(ot::InputEvent::kPadDown, 0, 100);
    r.loop();
    render(r);
    CHECK(gate(r, 0));
    r.push(ot::InputEvent::kPadUp, 0);
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
}

TEST(preview_releases_the_old_track_and_dialogs_ignore_repeats_and_shift) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);
    r.page(7);
    r.tap(b::Down);
    r.enc(1);              // preview on track 1
    shiftTap(r, b::Right); // track 2
    r.enc(1);              // preview on track 2
    r.loop(500);
    render(r);
    CHECK(!gate(r, 0));
    CHECK(!gate(r, 1));
    r.tap(b::F8);          // KEEP
    // INIT dialog opened by Delete: a repeat of the held key is not an answer.
    r.page(1);
    r.tap(b::Delete);
    r.tap(b::Delete);      // what the device's auto-repeat sends
    CHECK(r.app->ui.dialogOpen());
    r.tap(b::F7);
    // Instr does not open the picker under a dialog.
    r.tap(b::Delete);
    r.tap(b::Instr);
    CHECK(!r.app->ui.pickerOpen());
    // PASTE confirmed with Shift held still pastes (not a page jump).
    r.tap(b::F7);          // slot 1
    r.tap(b::F2);          // COPY
    r.tap(b::F8);          // slot 2: the kept preset, so PASTE asks
    r.tap(b::F3);
    CHECK(r.app->ui.dialogOpen());
    shiftTap(r, b::F8);
    CHECK_STR(r.app->song.inst[2].name, r.app->song.inst[1].name);
}

TEST(card_instrument_library_save_browse_try) {
    // UX plan P3.2: GENERAL F4 SAVE, SOUNDS CARD category.
    Rig r;
    r.tap(b::InstrParams);   // instrument 1 (PULSE LEAD)
    r.tap(b::F4);            // SAVE: the keyboard suggests PULSE_LEAD
    CHECK(r.app->ui.keyboardOpen());
    r.tap(b::F8);
    CHECK(r.storage.files.count("SIDTracker/Instruments/User/PULSE_LEAD.ins") == 1);
    r.tap(b::F4);
    r.tap(b::F8);            // same name: asks
    CHECK(r.app->ui.dialogOpen());
    r.tap(b::F7);
    // A second file in another folder.
    r.storage.dirs.insert("SIDTracker/Instruments/Bass");
    r.storage.files["SIDTracker/Instruments/Bass/ZBASS.ins"] = r.storage.files["SIDTracker/Instruments/User/PULSE_LEAD.ins"];
    // SOUNDS on slot 2: the CARD category (after the 7 built-in ones).
    r.tap(b::F8);
    r.page(7);
    r.enc(-1);               // CATEGORY wraps to CARD
    CHECK_EQ(r.app->instrumentCount(), 2);
    CHECK_STR(r.app->instrumentFile(0), "Bass/ZBASS.ins");
    CHECK_STR(r.app->instrumentFile(1), "User/PULSE_LEAD.ins");
    const Instrument before = r.app->song.inst[2];
    r.tap(b::Down);
    r.enc(1);                // try User/PULSE_LEAD
    CHECK_EQ(r.app->song.inst[2].pulsePtr != 0, true);
    CHECK_EQ(r.app->song.inst[2].sr, r.app->song.inst[1].sr);
    r.tap(b::F7);            // CANCEL
    CHECK(!std::memcmp(&r.app->song.inst[2], &before, sizeof before));
    r.enc(1);
    r.tap(b::F8);            // KEEP
    CHECK_EQ(r.app->song.inst[2].ad, r.app->song.inst[1].ad);
}

TEST(design_randomiser_tries_and_cancels) {
    // UX plan P3.3: Insert = variation, Shift+Insert = random; F7 / F8.
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);            // slot 2
    r.page(2);               // DESIGN
    Song& s = r.app->song;
    const Instrument before = s.inst[2];
    Table tables[kTableKinds];
    std::memcpy(tables, s.tables, sizeof tables);
    r.loop(5);
    r.push(ot::InputEvent::kButtonDown, b::Shift);
    r.tap(b::Insert);        // a random sound
    r.push(ot::InputEvent::kButtonUp, b::Shift);
    r.loop();
    CHECK(designer::isDesigned(s, 2));
    for (int i = 0; i < 20; ++i) {   // many tries never fill the tables
        r.loop(7);
        r.tap(b::Insert);
    }
    r.tap(b::F7);            // CANCEL
    CHECK(!std::memcmp(&s.inst[2], &before, sizeof before));
    CHECK(!std::memcmp(s.tables, tables, sizeof tables));
    r.tap(b::Insert);
    r.tap(b::F8);            // KEEP
    CHECK(designer::isDesigned(s, 2));
    CHECK_EQ(r.app->ui.inst, 2);
}

// --- Fable review 2026-10-22 (P3) ---------------------------------------------

TEST(card_category_edge_cases) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);            // slot 2
    r.app->song.inst[2].sr = 0x77;   // an edited slot: a load would ask first
    r.page(7);
    r.enc(-1);               // CARD, empty
    CHECK_EQ(r.app->instrumentCount(), 0);
    r.tap(b::Insert);        // nothing to load: no question
    CHECK(!r.app->ui.dialogOpen());
    // A save on GENERAL appears in CARD at once.
    r.page(1);
    r.tap(b::F4);
    r.tap(b::F8);            // keyboard OK
    r.page(7);
    CHECK_EQ(r.app->instrumentCount(), 1);
    // An unreadable file ends the try: F8 is NEXT INS again, slot unchanged.
    r.storage.files["SIDTracker/Instruments/AAA.ins"] = {'x', 'y'};
    r.app->scanInstruments();
    const Instrument before = r.app->song.inst[2];
    r.tap(b::Down);
    r.enc(0);
    r.push(ot::InputEvent::kEncoder, 0, 0, 1);
    r.push(ot::InputEvent::kEncoder, 0, 0, -1);   // back onto AAA.ins (index 0)
    r.loop();
    CHECK(!std::memcmp(&r.app->song.inst[2], &before, sizeof before));
    r.tap(b::F8);
    CHECK_EQ(r.app->ui.inst, 3);
}

TEST(shift_f_keys_jump_pages_while_trying_on_design) {
    Rig r;
    r.tap(b::InstrParams);
    r.tap(b::F8);
    r.page(2);
    r.tap(b::Insert);        // trying a variation
    const Instrument tried = r.app->song.inst[2];
    r.page(7);               // Shift+F7: SOUNDS, not CANCEL
    CHECK(!std::memcmp(&r.app->song.inst[2], &tried, sizeof tried));
    r.page(2);
    r.tap(b::F7);            // not trying any more: PREV INS
    CHECK_EQ(r.app->ui.inst, 1);
}

TEST(one_sid_song_plays_on_both_sides) {
    // Config: ONE SID ON BOTH SIDES copies SID 1 to SID 2 while SID 2 is unused.
    Rig r;
    int16_t buf[256];
    auto render = [&]() {
        for (int i = 0; i < 8; ++i) r.app->engine.fill(buf, 128);
    };
    r.app->song.pattern(0).cells[0][0] = {49, 1, 0, 0};   // SID 1 only
    r.app->setSidCopy(true);
    CHECK(r.app->sidCopyActive());
    r.app->engine.playSong(0);
    render();
    CHECK(r.app->engine.chip(1).shadow(4) & 0x01);              // SID 2 voice 1 gated: the copy
    CHECK_EQ(r.app->engine.chip(1).shadow(1), r.app->engine.chip(0).shadow(1));
    // A note on track 4: SID 2 has its own part again.
    r.app->song.pattern(0).cells[8][3] = {37, 1, 0, 0};
    r.loop(300);
    CHECK(!r.app->sidCopyActive());
    r.app->setSidCopy(false);
    CHECK(!r.app->sidCopyActive());
    r.loop(1100);   // the setting is saved
    CHECK(r.storage.files.count("SIDTracker/settings.txt") == 1);
}

TEST(instrument_column_works_like_the_stock_tracker) {
    // Pads in the instrument column are instruments 1-48; holding Instr and
    // releasing writes the choice in edit mode; Delete clears the step.
    Rig r;
    const Pattern& p = r.app->song.pattern(0);
    r.tap(b::Instr);         // instrument column (a tap: no picker)
    CHECK(!r.app->ui.pickerOpen());
    r.pad(6);                // not in edit mode: instrument 7 becomes current, nothing written
    CHECK_EQ(r.app->ui.inst, 7);
    CHECK_EQ(p.cells[0][0].inst, 0);
    r.tap(b::Rec);
    r.pad(4);                // instrument 5 into step 0, with a note; the cursor moves on
    CHECK_EQ(p.cells[0][0].inst, 5);
    CHECK(p.cells[0][0].note != 0);
    CHECK_EQ(r.app->ui.inst, 5);
    r.pad(11);               // step 1: instrument 12
    CHECK_EQ(p.cells[1][0].inst, 12);
    // Hold Instr, pick 20 on the picker (jog), release: written into step 2.
    r.push(ot::InputEvent::kButtonDown, b::Instr);
    r.loop(250);
    CHECK(r.app->ui.pickerOpen());
    r.enc(8);                // 12 -> 20
    r.push(ot::InputEvent::kButtonUp, b::Instr);
    r.loop();
    CHECK_EQ(p.cells[2][0].inst, 20);
    // Delete in the instrument column clears the step.
    r.tap(b::Up);
    r.tap(b::Up);            // step 0 (the cursor stayed on step 2 after the picker)
    r.tap(b::Delete);
    CHECK(p.cells[0][0].empty());
}

TEST(sid_copy_review_fixes) {
    // Fable review 2026-10-22 (SID copy).
    Rig r;
    int16_t buf[256];
    auto render = [&](int n) {
        for (int i = 0; i < n; ++i) r.app->engine.fill(buf, 128);
    };
    r.app->song.pattern(0).cells[0][1] = {49, 1, 0, 0};   // track 2 holds a note
    r.app->setSidCopy(true);
    r.app->engine.playSong(0);
    render(20);
    CHECK(r.app->engine.chip(1).shadow(11) & 1);           // the copy plays
    // 1. Copy off: SID 2 is not left ringing, even after Stop.
    r.app->setSidCopy(false);
    r.app->engine.stop();
    render(40);
    CHECK(!(r.app->engine.chip(1).shadow(11) & 1));
    // 2. The cursor on track 4: the copy is off, and an audition there sounds.
    r.app->setSidCopy(true);
    r.loop(10);
    CHECK(r.app->sidCopyActive());
    for (int i = 0; i < 3; ++i) {
        r.push(ot::InputEvent::kButtonDown, b::Shift);
        r.tap(b::Right);
        r.push(ot::InputEvent::kButtonUp, b::Shift);
        r.loop();
    }
    CHECK_EQ(r.app->ui.channel, 3);
    CHECK(!r.app->sidCopyActive());
    r.push(ot::InputEvent::kPadDown, 36, 100);
    r.loop();
    render(10);
    CHECK(r.app->engine.chip(1).shadow(4) & 1);            // SID 2 voice 1 gated
    r.push(ot::InputEvent::kPadUp, 36);
    r.loop();
    // 3. A panned voice is not doubled by the copy.
    Rig q;
    q.app->song.pattern(0).cells[0][0] = {49, 1, 0, 0};
    q.app->song.mod.pan[0] = 1;
    q.app->engine.playSong(0);
    int16_t qb[256];
    int off = 0, on = 0;
    for (int i = 0; i < 30; ++i) {
        q.app->engine.fill(qb, 128);
        for (int k = 0; k < 256; ++k) off = std::max(off, std::abs(int(qb[k])));
    }
    q.app->setSidCopy(true);
    q.app->engine.stop();
    q.app->engine.playSong(0);
    for (int i = 0; i < 30; ++i) {
        q.app->engine.fill(qb, 128);
        for (int k = 0; k < 256; ++k) on = std::max(on, std::abs(int(qb[k])));
    }
    CHECK(on < off * 3 / 2);
}
