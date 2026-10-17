// Tracker playback host tests (PLAN.md §18: tracker timing, command
// decoding, table execution). The expected behaviour is GoatTracker 2's,
// as documented and as measured with tools/oracle (docs/goattracker-format.md).
#include <memory>
#include <string>
#include <vector>

#include "sid/regs.h"
#include "test.h"
#include "tracker/commands/commands.h"
#include "tracker/playback/player.h"

using namespace sidt;

namespace {

struct Recorder : ISidSink {
    uint8_t regs[2][32] = {};
    uint32_t writes = 0;
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override {
        regs[chip][reg] = val;
        ++writes;
    }
};

struct Fixture {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    Song song;
    Recorder rec;
    Player player;
    Fixture() {
        song.patterns = pats.get();
        song.clear();
        player.init(&song, &rec);
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) player.tick();
    }
    Cell& cell(int pat, int row, int ch) { return song.pattern(pat).cells[row][ch]; }
    uint16_t freq(int v = 0, int chip = 0) const {
        return uint16_t(rec.regs[chip][v * 7] | (rec.regs[chip][v * 7 + 1] << 8));
    }
    uint16_t pw(int v = 0) const { return uint16_t(rec.regs[0][v * 7 + 2] | ((rec.regs[0][v * 7 + 3] & 15) << 8)); }
    uint8_t ctl(int v = 0, int chip = 0) const { return rec.regs[chip][v * 7 + 4]; }
    // Instrument 2: saw at the note's pitch, AD 00 SR F0, no tables but wave.
    void sawInstrument(uint8_t gateTimer = 2) {
        Instrument& in = song.inst[2];
        in.ad = 0x00;
        in.sr = 0xF0;
        in.firstWave = 0x09;
        in.wavePtr = 0x20;
        in.pulsePtr = in.filterPtr = 0;
        in.gateTimer = gateTimer;
        song.tables[kWaveTable].left[0x1F] = 0x21;
        song.tables[kWaveTable].right[0x1F] = 0x00;
        song.tables[kWaveTable].left[0x20] = 0xFF;
        song.tables[kWaveTable].right[0x20] = 0x00;
    }
};

constexpr uint8_t C4 = 1 + 4 * 12;   // note value of C-4 (semitone 48)

}  // namespace

TEST(samples_per_tick_follow_bpm) {
    Fixture f;
    CHECK_EQ(f.player.samplesPerTick16(44100000) >> 16, 882u);   // 125 BPM = 50 Hz
    f.song.bpm = 150;
    CHECK_EQ(f.player.samplesPerTick16(44100000) >> 16, 735u);
}

TEST(goattracker_note_table) {
    FreqTable t;
    t.build(sid::kPalClockHz);
    CHECK_EQ(t.freq(0), 0x0117);    // C-0
    CHECK_EQ(t.freq(57), 0x1D46);   // A-4
    CHECK_EQ(t.freq(95), 0xFFFF);   // B-7
    FreqTable n;
    n.build(sid::kNtscClockHz);     // same table, as in GoatTracker
    CHECK_EQ(n.freq(57), 0x1D46);
}

TEST(rows_advance_every_speed_ticks_and_order_repeats) {
    Fixture f;
    f.song.speed = 3;
    f.song.pattern(0).rows = 4;
    f.song.pattern(1).rows = 2;
    f.song.orderLen = 2;
    f.song.order[0] = {0, 0, 2};
    f.song.order[1] = {1, 0, 1};
    f.player.playSong(0);
    f.ticks(3);
    CHECK_EQ(f.player.row(), 1);
    f.ticks(3 * 3);
    CHECK_EQ(f.player.row(), 0);
    CHECK_EQ(f.player.orderPos(), 0);
    f.ticks(3 * 4);
    CHECK_EQ(f.player.orderPos(), 1);
    f.ticks(3 * 2);
    CHECK_EQ(f.player.orderPos(), 0);
}

TEST(note_init_then_tables_next_tick) {
    Fixture f;
    f.cell(0, 0, 0) = {C4, 1, 0, 0};
    f.player.playSong(0);
    f.ticks(1);   // tick 0: init, first-frame wave 09 (test + gate), ADSR
    CHECK_EQ(f.ctl(), 0x09);
    CHECK_EQ(f.rec.regs[0][sid::kAD], f.song.inst[1].ad);
    CHECK_EQ(f.pw(), 0);   // the pulse table waits a tick too
    f.ticks(1);   // tick 1: wave table row 1 (41 00), pulse table row 1 (84 00)
    CHECK_EQ(f.ctl(), 0x41);
    CHECK_EQ(f.freq(), 0x1168);   // C-4 in GoatTracker's table
    CHECK_EQ(f.pw(), 0x400);
}

TEST(channels_map_to_chips_and_voices) {
    Fixture f;
    f.cell(0, 0, 4) = {C4, 1, 0, 0};   // channel 5 -> SID2 voice 2
    f.player.playSong(0);
    f.ticks(2);
    CHECK(f.ctl(1, 1) & sid::kGate);
    CHECK(!(f.ctl(0, 0) & sid::kGate));
}

TEST(only_changed_registers_are_written) {
    Fixture f;
    f.sawInstrument();
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(3);
    const uint32_t before = f.rec.writes;
    f.ticks(1);
    CHECK_EQ(f.rec.writes, before);
}

TEST(hard_restart_on_the_fetch_tick) {
    Fixture f;
    f.sawInstrument(2);
    f.song.speed = 6;
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.cell(0, 1, 0) = {C4 + 2, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(4);   // ticks 0..3
    CHECK(f.ctl() & sid::kGate);
    f.ticks(1);   // tick 4 = speed - gate timer: fetch, gate off, HR ADSR
    CHECK(!(f.ctl() & sid::kGate));
    CHECK_EQ(f.rec.regs[0][sid::kAD], f.song.hrAD);
    CHECK_EQ(f.rec.regs[0][sid::kSR], f.song.hrSR);
    f.ticks(2);   // next row tick 0: init with gate on
    CHECK_EQ(f.ctl(), 0x09);
    CHECK_EQ(f.rec.regs[0][sid::kAD], 0x00);
}

TEST(gate_timer_flags) {
    Fixture f;
    f.sawInstrument(0x80 | 2);   // no hard restart: gate off only
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.cell(0, 1, 0) = {C4 + 2, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(5);
    CHECK(!(f.ctl() & sid::kGate));
    CHECK_EQ(f.rec.regs[0][sid::kAD], 0x00);   // ADSR kept
    Fixture g;
    g.sawInstrument(0x40 | 2);   // legato: neither
    g.cell(0, 0, 0) = {C4, 2, 0, 0};
    g.cell(0, 1, 0) = {C4 + 2, 2, 0, 0};
    g.player.playSong(0);
    g.ticks(5);
    CHECK(g.ctl() & sid::kGate);
}

TEST(keyoff_at_fetch_and_wave_gate_bit) {
    Fixture f;
    f.sawInstrument(2);
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.cell(0, 1, 0) = {note::kKeyOff, 0, 0, 0};
    f.player.playSong(0);
    f.ticks(4);
    CHECK(f.ctl() & sid::kGate);
    f.ticks(1);   // tick 4: the key-off row is fetched -> gate off
    CHECK_EQ(f.ctl(), 0x20);
    // A wave table value without the gate bit clears the gate as well.
    Fixture g;
    g.sawInstrument();
    g.song.tables[kWaveTable].left[0x1F] = 0x20;
    g.cell(0, 0, 0) = {C4, 2, 0, 0};
    g.player.playSong(0);
    g.ticks(2);
    CHECK_EQ(g.ctl(), 0x20);
}

TEST(wave_table_delay_relative_absolute_jump) {
    Fixture f;
    Instrument& in = f.song.inst[2];
    in.firstWave = 0x09;
    in.wavePtr = 10;
    Table& w = f.song.tables[kWaveTable];
    w.left[9] = 0x21;  w.right[9] = 0x00;    // 10: saw, +0
    w.left[10] = 0x02; w.right[10] = 0x0C;   // 11: wait 2, then +12
    w.left[11] = 0x00; w.right[11] = 0x81;   // 12: absolute C#0
    w.left[12] = 0xFF; w.right[12] = 10;     // 13: jump to 10
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    FreqTable t;
    t.build(sid::kPalClockHz);
    f.player.playSong(0);
    f.ticks(2);   // init, row 10
    CHECK_EQ(f.ctl(), 0x21);
    CHECK_EQ(f.freq(), t.freq(48));
    f.ticks(2);   // row 11 waits two ticks
    CHECK_EQ(f.freq(), t.freq(48));
    f.ticks(1);   // ... then +12
    CHECK_EQ(f.freq(), t.freq(60));
    f.ticks(1);   // row 12: absolute C#0
    CHECK_EQ(f.freq(), t.freq(1));
    f.ticks(1);   // jump to 10, executed on the same tick
    CHECK_EQ(f.freq(), t.freq(48));
}

TEST(wave_table_command_acts_once) {
    Fixture f;
    Instrument& in = f.song.inst[2];
    in.wavePtr = 10;
    Table& w = f.song.tables[kWaveTable];
    w.left[9] = 0x21;  w.right[9] = 0x00;
    w.left[10] = 0xF1; w.right[10] = 0x01;   // porta up, speed row 1, once
    w.left[11] = 0xF6; w.right[11] = 0x2A;   // SR = 2A
    w.left[12] = 0xFF; w.right[12] = 0x00;
    f.song.tables[kSpeedTable].left[0] = 0x00;
    f.song.tables[kSpeedTable].right[0] = 0x40;
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(3);
    CHECK_EQ(f.freq(), 0x1168 + 0x40);
    f.ticks(1);
    CHECK_EQ(f.rec.regs[0][sid::kSR], 0x2A);
    f.ticks(3);
    CHECK_EQ(f.freq(), 0x1168 + 0x40);   // no running portamento
}

TEST(pulse_table_sweeps_wraps_and_00_is_256_ticks) {
    Fixture f;
    Instrument& in = f.song.inst[2];
    in.pulsePtr = 1;
    in.wavePtr = 0;
    in.gateTimer = 0;   // no fetch-tick pulse skip in this test
    Table& p = f.song.tables[kPulseTable];
    p = Table{};
    p.left[0] = 0x8F; p.right[0] = 0xF0;   // set 0xFF0
    p.left[1] = 0x00; p.right[1] = 0x10;   // +0x10 for 256 ticks
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.song.speed = 31;
    f.song.pattern(0).rows = 64;
    f.player.playSong(0);
    f.ticks(2);
    CHECK_EQ(f.pw(), 0xFF0);
    f.ticks(1);
    CHECK_EQ(f.pw(), 0x000);   // 12-bit wrap
    f.ticks(9);
    CHECK_EQ(f.pw(), 0x090);
}

TEST(filter_table_after_init_and_commands) {
    Fixture f;
    Instrument& in = f.song.inst[2];
    in.filterPtr = 1;
    Table& ft = f.song.tables[kFilterTable];
    ft.left[0] = 0x90; ft.right[0] = 0xF1;   // LP, res F, voice 1 ...
    ft.left[1] = 0x00; ft.right[1] = 0x40;   // ... and cutoff 40 on the same tick
    ft.left[2] = 0x02; ft.right[2] = 0x08;   // +8 x 2
    ft.left[3] = 0xFF; ft.right[3] = 0x00;
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(1);   // init tick: the filter table starts next tick
    CHECK_EQ(f.rec.regs[0][sid::kResFilt], 0x00);
    f.ticks(1);
    CHECK_EQ(f.rec.regs[0][sid::kResFilt], 0xF1);
    CHECK_EQ(f.rec.regs[0][sid::kModeVol], 0x1F);
    CHECK_EQ(f.rec.regs[0][sid::kFcHi], 0x40);
    f.ticks(2);
    CHECK_EQ(f.rec.regs[0][sid::kFcHi], 0x50);
}

TEST(one_shot_commands_on_tick_0) {
    Fixture f;
    f.cell(0, 0, 3) = {C4, 1, cmd::kSetSR, 0x5A};
    f.cell(0, 0, 1) = {0, 0, cmd::kVolume, 0x07};
    f.cell(0, 0, 2) = {0, 0, cmd::kTempo, 0x03};
    f.player.playSong(0);
    f.ticks(1);
    CHECK_EQ(f.rec.regs[1][sid::kSR], 0x5A);
    // Filter and volume registers are written before the tracks run: a
    // tick-0 command shows on the next tick (as in GoatTracker).
    CHECK_EQ(f.rec.regs[0][sid::kModeVol] & 15, 15);
    f.ticks(1);
    CHECK_EQ(f.rec.regs[0][sid::kModeVol] & 15, 7);
    CHECK_EQ(f.player.speed(), 3);
    // B00: filter off and the filter table stopped.
    Fixture g;
    g.song.inst[1].filterPtr = 1;
    g.song.tables[kFilterTable].left[0] = 0x90;
    g.song.tables[kFilterTable].right[0] = 0xF1;
    g.cell(0, 0, 0) = {C4, 1, 0, 0};
    g.cell(0, 1, 0) = {0, 0, cmd::kFilterCtrl, 0x00};
    g.player.playSong(0);
    g.ticks(8);
    CHECK_EQ(g.rec.regs[0][sid::kResFilt], 0x00);
}

TEST(portamento_skips_tick_0_and_tone_portamento) {
    Fixture f;
    f.sawInstrument();
    Table& sp = f.song.tables[kSpeedTable];
    sp.left[0] = 0x00; sp.right[0] = 0x40;
    f.song.speed = 4;
    f.cell(0, 0, 0) = {C4, 2, cmd::kPortaUp, 1};
    f.cell(0, 1, 0) = {0, 0, cmd::kPortaUp, 1};
    f.player.playSong(0);
    f.ticks(2);   // init, wave sets C-4 (no portamento on that tick)
    CHECK_EQ(f.freq(), 0x1168);
    f.ticks(2);   // ticks 2, 3
    CHECK_EQ(f.freq(), 0x1168 + 2 * 0x40);
    f.ticks(1);   // tick 0 of row 1: skipped
    CHECK_EQ(f.freq(), 0x1168 + 2 * 0x40);
    f.ticks(1);
    CHECK_EQ(f.freq(), 0x1168 + 3 * 0x40);
    // Tone portamento runs while rows carry command 3 (0 ends it).
    f.cell(0, 2, 0) = {C4 + 12, 0, cmd::kTonePorta, 1};
    for (int r = 3; r < 64; ++r) f.cell(0, r, 0) = {0, 0, cmd::kTonePorta, 1};
    f.ticks(200);
    FreqTable t;
    t.build(sid::kPalClockHz);
    CHECK_EQ(f.freq(), t.freq(60));
}

TEST(vibrato_direction_runs_measured_in_goattracker) {
    // Speed 3: the first half cycle is 2 steps up, then 5 down, 5 up...
    Fixture f;
    f.sawInstrument();
    f.song.tables[kSpeedTable].left[0] = 3;
    f.song.tables[kSpeedTable].right[0] = 1;
    f.song.speed = 64;
    f.song.pattern(0).rows = 4;
    f.cell(0, 0, 0) = {C4, 2, cmd::kVibrato, 1};
    f.player.playSong(0);
    f.ticks(2);
    std::string s;
    uint16_t last = f.freq();
    for (int i = 0; i < 17; ++i) {
        f.ticks(1);
        s += f.freq() > last ? '+' : (f.freq() < last ? '-' : '.');
        last = f.freq();
    }
    CHECK_STR(s.c_str(), "++-----+++++-----");
}

TEST(instrument_vibrato_delay_counts_free_ticks) {
    Fixture f;
    f.sawInstrument();
    Instrument& in = f.song.inst[2];
    in.vibParam = 1;
    in.vibDelay = 4;
    f.song.tables[kSpeedTable].left[0] = 3;
    f.song.tables[kSpeedTable].right[0] = 1;
    f.song.speed = 64;
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(2);   // init, tick 1 (wave sets the pitch: not counted)
    std::string s;
    uint16_t last = f.freq();
    for (int i = 0; i < 6; ++i) {
        f.ticks(1);
        s += f.freq() > last ? '+' : (f.freq() < last ? '-' : '.');
        last = f.freq();
    }
    CHECK_STR(s.c_str(), "...++-");   // starts on the 4th free tick
}

TEST(transpose_and_preview) {
    Fixture f;
    f.song.order[0].transpose = 12;
    f.cell(0, 0, 0) = {C4, 1, 0, 0};
    f.player.playSong(0);
    f.ticks(2);
    CHECK_EQ(f.player.channelNote(0), 60);
    f.player.stop();
    f.ticks(1);
    CHECK(!(f.ctl() & sid::kGate));
    f.player.noteOn(2, C4 + 1, 1);
    f.ticks(2);
    CHECK(f.ctl(2) & sid::kGate);
    f.player.noteOff(2);
    f.ticks(1);
    CHECK(!(f.ctl(2) & sid::kGate));
}

TEST(perform_overrides) {
    Fixture f;
    f.cell(0, 0, 0) = {C4, 1, 0, 0};
    f.player.playSong(0);
    f.ticks(2);
    f.player.perform.wave[0] = sid::kSaw;
    f.player.perform.cutoffOffset[0] = 2047;
    f.player.perform.resonance[0] = 9;
    f.ticks(1);
    CHECK_EQ(f.ctl() & 0xF0, sid::kSaw);
    CHECK_EQ(f.rec.regs[0][sid::kFcHi], 0xFF);
    CHECK_EQ(f.rec.regs[0][sid::kResFilt] >> 4, 9);
}

TEST(command_names) {
    CHECK_STR(cmd::name(cmd::kTonePorta), "TONE PORTA");
    CHECK(cmd::usesSpeedTable(cmd::kVibrato));
    CHECK(!cmd::usesSpeedTable(cmd::kSetAD));
    char n[4];
    noteName(C4, n);
    CHECK_STR(n, "C-4");
    noteName(C4 + 1, n);
    CHECK_STR(n, "C#4");
    noteName(note::kKeyOff, n);
    CHECK_STR(n, "===");
}

TEST(perform_automation_applies_on_row_start) {
    Fixture f;
    f.sawInstrument();
    f.song.speed = 4;
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.song.pattern(0).setAuto(1, kAutoCutoff + 0, 800);
    f.song.pattern(0).setAuto(1, kAutoWave + 0, sid::kPulse);
    f.song.pattern(0).setAuto(2, kAutoWave + 0, 0);
    f.player.playSong(0);
    f.ticks(4);
    CHECK_EQ(f.player.perform.cutoffOffset[0], 0);
    f.ticks(1);   // row 1, tick 0
    CHECK_EQ(f.player.perform.cutoffOffset[0], 800);
    CHECK_EQ(f.ctl() & 0xF0, sid::kPulse);
    f.ticks(4);   // row 2: back to the song's waveform
    CHECK_EQ(f.ctl() & 0xF0, sid::kSaw);
    CHECK_EQ(f.song.automatedLanes(), uint16_t((1u << kAutoCutoff) | (1u << kAutoWave)));
    f.player.resetPerformLanes(f.song.automatedLanes());
    CHECK_EQ(f.player.perform.cutoffOffset[0], 0);
    f.player.setPerformLane(kAutoRes + 1, 99);
    CHECK_EQ(f.player.perform.resonance[1], 15);
}

TEST(lfo_modulates_pulse_width) {
    Fixture f;
    f.sawInstrument();
    f.song.inst[2].pulsePtr = 0;
    f.song.mod.lfo[0] = Lfo{1, Lfo::kSquare, 4, 50};   // PW track 1, +-1000
    f.cell(0, 0, 0) = {C4, 2, 0, 0};
    f.player.playSong(0);
    f.ticks(2);
    const uint16_t a = f.pw();
    f.ticks(2);
    const uint16_t b = f.pw();
    CHECK(a != b);   // the square flipped between the two reads
    CHECK(f.song.extendedFeatures() & kExtModulation);
}

TEST(rows_without_early_fetch_play_in_order) {
    // Gate timer 0 (and a gate timer >= speed): no fetch before the row;
    // tick 0 must read the current row, not the one after it.
    for (uint8_t gt : {uint8_t(0), uint8_t(2)}) {
        Fixture f;
        f.sawInstrument(gt);
        f.song.speed = 2;
        for (int r = 0; r < 4; ++r) f.cell(0, r, 0) = {uint8_t(C4 + r), 2, 0, 0};
        f.player.playSong(0);
        for (int r = 0; r < 4; ++r) {
            f.ticks(2);
            CHECK_EQ(f.player.channelNote(0), 48 + r);
        }
    }
}
