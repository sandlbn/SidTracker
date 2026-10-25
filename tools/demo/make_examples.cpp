// make_examples -- writes SIDTracker's original example songs (MIT, made for
// the release zip; no third-party music):
//   NIGHTDRIVE  one SID (6581): drums woven into the bass track, arps, lead
//   PCMGROOVE   two SIDs + PCM drum column, delay, reverb, LFO, voice pan
//   FILTERS     two SIDs (8580): slow resonant filter sweeps, 100 bpm (CIA)
//   CHIPMARCH   one SID (8580): swing tempo, slides, tone portamento, vibrato
//   HYPERARP    one SID (6581), multispeed x4: 200 Hz chord arpeggios, smooth
//               vibrato, a kick with a fine pitch drop
//   SILKSWEEP   two SIDs (8580), multispeed x2: fine pulse / filter sweeps,
//               smooth slides, an echo lead on SID 2
//   CRACKINTRO  one SID (6581), demoscene: cracktro tune from library
//               instruments (bass + drums on one track, arps, slide lead)
//   RASTERBARS  two SIDs (8580), demoscene: driving demo part (wobble bass,
//               power chords, saw lead, screaming echo on SID 2)
//   ENDSCROLL   two SIDs (8580), demoscene: slow end part (pads, strings,
//               flute melody, marimba, ring bell)
//   make_examples OUTDIR
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "tracker/commands/commands.h"
#include "tracker/song/project_file.h"
#include "tracker/song/sound_library.h"
#include "classic_instruments.h"

using namespace sidt;

namespace {

struct FileWriter : project::Writer {
    std::FILE* f;
    bool write(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};

uint8_t nt(int octave, int semitone) {
    while (semitone < 0) {
        semitone += 12;
        --octave;
    }
    return uint8_t(1 + (octave + semitone / 12) * 12 + semitone % 12);
}

struct TableWriter {
    Table& t;
    int row = 0;
    uint8_t add(std::initializer_list<std::pair<uint8_t, uint8_t>> rows) {
        const uint8_t start = uint8_t(row + 1);
        for (auto& r : rows) {
            t.left[row] = r.first;
            t.right[row] = r.second;
            ++row;
        }
        return start;
    }
    // A loop back to `to` at the last added row.
    void loopTo(uint8_t to) { t.right[row - 1] = to; }
};

// The instrument set every example draws from.
struct Kit {
    uint8_t bass = 1, arpMin = 2, arpMaj = 3, lead = 4, kick = 5, snare = 6, hat = 7, pad = 8, echo = 9,
            acid = 10, stab = 11, sweep = 12, bell = 13, slideLead = 14;
};

void inst(Song& s, uint8_t i, const char* name, uint8_t ad, uint8_t sr, uint8_t wave, uint8_t pulse, uint8_t filter,
          uint8_t vib = 0, uint8_t vibDelay = 0, uint8_t gate = 2, uint8_t first = 0x09) {
    Instrument& in = s.inst[i];
    std::snprintf(in.name, sizeof in.name, "%s", name);
    in.ad = ad;
    in.sr = sr;
    in.firstWave = first;
    in.wavePtr = wave;
    in.pulsePtr = pulse;
    in.filterPtr = filter;
    in.vibParam = vib;
    in.vibDelay = vibDelay;
    in.gateTimer = gate;
}

Kit buildKit(Song& s, uint8_t filterRoute1, uint8_t filterRoute2) {
    Kit k;
    for (Table& t : s.tables) t = Table{};
    for (int i = 1; i < kInstrumentCount; ++i) s.inst[i].clear(i);
    TableWriter w{s.tables[kWaveTable]}, pu{s.tables[kPulseTable]}, fi{s.tables[kFilterTable]},
        sp{s.tables[kSpeedTable]};
    const uint8_t vib = sp.add({{0x04, 0x18}});
    const uint8_t vibWide = sp.add({{0x03, 0x30}});
    sp.add({{0x00, 0x40}});   // row 3: porta speed $40 (commands 1 / 2 / 3)
    sp.add({{0x00, 0xA0}});   // row 4: porta speed $A0
    sp.add({{0x04, 0x03}});   // row 5: funk tempo 4 / 3
    const uint8_t wPulse = w.add({{0x41, 0x00}, {0xFF, 0x00}});
    const uint8_t wMin = w.add({{0x41, 0x00}, {0x41, 0x03}, {0x41, 0x07}, {0xFF, 0x00}});
    w.loopTo(wMin);
    const uint8_t wMaj = w.add({{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0xFF, 0x00}});
    w.loopTo(wMaj);
    const uint8_t wLead = w.add({{0x21, 0x0C}, {0x41, 0x00}, {0xFF, 0x00}});
    const uint8_t wKick = w.add({{0x81, 0xC4}, {0x41, 0xA8}, {0x41, 0xA2}, {0x11, 0x9E}, {0x11, 0x9A}, {0x10, 0x96},
                                 {0xFF, 0x00}});
    const uint8_t wSnare = w.add({{0x81, 0xD8}, {0x41, 0xAA}, {0x81, 0xD4}, {0x80, 0xD1}, {0x80, 0xCE}, {0x80, 0xCC},
                                  {0xFF, 0x00}});
    const uint8_t wHat = w.add({{0x81, 0xDF}, {0x80, 0xDF}, {0xFF, 0x00}});
    const uint8_t wSaw = w.add({{0x21, 0x00}, {0xFF, 0x00}});
    const uint8_t wEcho = w.add({{0x41, 0x0C}, {0xFF, 0x00}});
    const uint8_t wStab = w.add({{0x81, 0x30}, {0x41, 0x00}, {0x41, 0x07}, {0x41, 0x0C}, {0x40, 0x0C}, {0xFF, 0x00}});
    const uint8_t wBell = w.add({{0x15, 0x00}, {0x11, 0x0C}, {0x11, 0x00}, {0xFF, 0x00}});
    const uint8_t pBass = pu.add({{0x82, 0x00}, {0x20, 0x30}, {0x20, 0xD0}, {0xFF, 0x02}});
    const uint8_t pChord = pu.add({{0x84, 0x00}, {0x40, 0x10}, {0x40, 0xF0}, {0xFF, 0x02}});
    const uint8_t pLead = pu.add({{0x86, 0x00}, {0x30, 0x18}, {0x30, 0xE8}, {0xFF, 0x02}});
    const uint8_t pDrum = pu.add({{0x88, 0x00}, {0xFF, 0x00}});
    const uint8_t pEcho = pu.add({{0x83, 0x00}, {0x7F, 0x08}, {0xFF, 0x00}});
    const uint8_t pThin = pu.add({{0x81, 0x00}, {0x60, 0x04}, {0x60, 0xFC}, {0xFF, 0x02}});
    // Filters: chords on voice 2 of SID1, a band-pass pad sweep on SID2's
    // voice 2, an acid low pass on voice 1, a slow high-to-low sweep.
    const uint8_t fChord = fi.add({{0x90, uint8_t(0xA0 | filterRoute1)}, {0x00, 0x28}, {0x20, 0x02}, {0x20, 0xFE},
                                   {0xFF, 0x03}});
    const uint8_t fPad = fi.add({{0xA0, uint8_t(0xC0 | filterRoute2)}, {0x00, 0x10}, {0x60, 0x01}, {0x60, 0xFF},
                                 {0xFF, 0x0B}});
    const uint8_t fAcid = fi.add({{0x90, 0xF1}, {0x00, 0x70}, {0x0C, 0xF8}, {0x10, 0x00}, {0xFF, 0x00}});
    const uint8_t fSweep = fi.add({{0x90, uint8_t(0xE0 | filterRoute2)}, {0x00, 0x08}, {0x7F, 0x01}, {0x7F, 0xFF},
                                   {0xFF, 0x12}});
    inst(s, k.bass, "PULSE BASS", 0x0A, 0x8A, wPulse, pBass, 0);
    inst(s, k.arpMin, "ARP MINOR", 0x00, 0xA8, wMin, pChord, fChord);
    inst(s, k.arpMaj, "ARP MAJOR", 0x00, 0xA8, wMaj, pChord, fChord);
    inst(s, k.lead, "PULSE LEAD", 0x0A, 0xAA, wLead, pLead, 0, vib, 10);
    inst(s, k.kick, "KICK", 0x00, 0xE9, wKick, pDrum, 0, 0, 0, 0);
    inst(s, k.snare, "SNARE", 0x00, 0xF9, wSnare, pDrum, 0, 0, 0, 0);
    inst(s, k.hat, "HIHAT", 0x00, 0x00, wHat, 0, 0, 0, 0, 0);
    inst(s, k.pad, "SAW PAD", 0x8A, 0xCB, wSaw, 0, fPad, 0, 0, 0x42);
    inst(s, k.echo, "ECHO LEAD", 0x2A, 0x6A, wEcho, pEcho, 0, vib, 12);
    inst(s, k.acid, "ACID BASS", 0x00, 0xA9, wSaw, 0, fAcid, 0, 0, 2);
    inst(s, k.stab, "CHORD STAB", 0x00, 0x89, wStab, pChord, 0, 0, 0, 2);
    inst(s, k.sweep, "SWEEP PAD", 0xC9, 0xAC, wSaw, 0, fSweep, vibWide, 30, 0x42);
    inst(s, k.bell, "RING BELL", 0x09, 0x0A, wBell, 0, 0, 0, 0, 2, 0x15);
    inst(s, k.slideLead, "SLIDE LEAD", 0x0A, 0xAA, wPulse, pThin, 0, vib, 6);
    return k;
}

struct Chord {
    int root;
    bool minor;
};

struct Ev {
    int row, oct, semi;
    uint8_t cmd = 0, param = 0;
};

void put(Pattern& p, int row, int ch, uint8_t n, uint8_t i, uint8_t cmd = 0, uint8_t par = 0) {
    if (row >= 0 && row < p.rows) p.cells[row][ch] = {n, i, cmd, par};
}

bool save(const Song& s, const std::string& path) {
    FileWriter fw;
    fw.f = std::fopen(path.c_str(), "wb");
    if (!fw.f) return std::perror(path.c_str()), false;
    const bool ok = project::save(s, fw) == project::Result::kOk;
    std::fclose(fw.f);
    std::printf("%s: %s\n", path.c_str(), ok ? "written" : "write error");
    return ok;
}

struct Box {
    std::unique_ptr<Pattern[]> pats{new Pattern[kPatternCount]};
    std::unique_ptr<Song> song{new Song};
    Box() {
        song->patterns = pats.get();
        song->clear();
    }
};

void order(Song& s, std::initializer_list<int> pats, int restart) {
    s.orderLen = 0;
    for (int p : pats) s.order[s.orderLen++] = {uint8_t(p), 0, 1};
    s.restartPos = uint8_t(restart);
}

// --- NIGHTDRIVE: one SID ---------------------------------------------------
bool nightDrive(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "NIGHT DRIVE");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.speed = 6;
    s.sid.model[0] = sid::Model::k6581;
    s.sid.balance = 0;
    const Kit k = buildKit(s, 0x02, 0x02);
    const Chord prog[4] = {{2, true}, {10, false}, {5, false}, {0, false}};   // Dm Bb F C
    // Lead over 4 bars (64 rows).
    const Ev mel[] = {{0, 5, 2},  {6, 5, 5},  {8, 5, 9},  {12, 5, 7},  {14, 5, 5},  {16, 5, 2},  {22, 4, 10},
                      {24, 5, 2}, {28, 5, 5}, {32, 5, 0}, {38, 5, 5},  {40, 5, 9},  {44, 5, 7},  {48, 5, 7},
                      {52, 5, 4, cmd::kTonePorta, 3}, {56, 5, 0},  {60, 4, 10}};
    for (int p = 0; p < 4; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            for (int r = 0; r < 16; r += 2) {
                // Track 1: kick on 0 / 8, snare on 4 / 12 (from pattern 1 on), bass between.
                if (r == 0 || r == 8) put(pt, r0 + r, 0, nt(3, 0), k.kick);
                else if ((r == 4 || r == 12) && p >= 1) put(pt, r0 + r, 0, nt(4, 0), k.snare);
                else put(pt, r0 + r, 0, nt(r % 4 ? 3 : 2, c.root), k.bass);
            }
            if (p >= 1) {
                put(pt, r0, 1, nt(4, c.root), c.minor ? k.arpMin : k.arpMaj);
                put(pt, r0 + 8, 1, nt(4, c.root), c.minor ? k.arpMin : k.arpMaj);
            }
        }
        if (p >= 2)
            for (const Ev& e : mel) put(pt, e.row, 2, nt(e.oct, e.semi), k.lead, e.cmd, e.param);
        if (p == 3)   // the lead an octave up the second time round
            for (const Ev& e : mel) put(pt, e.row, 2, nt(e.oct + 1, e.semi), k.echo, e.cmd, e.param);
    }
    order(s, {0, 1, 2, 3, 2, 1}, 1);
    return save(s, dir + "/NIGHTDRIVE.sidt");
}

// --- PCMGROOVE: two SIDs + extended engine ----------------------------------
bool pcmGroove(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "PCM GROOVE");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.mode = SongMode::kExtended;
    s.speed = 6;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k8580;
    s.sid.balance = 70;
    const Kit k = buildKit(s, 0x02, 0x02);
    s.fx.delayMix = 28;
    s.fx.delayTicks = 9;   // a dotted eighth at speed 6
    s.fx.delayFeedback = 45;
    s.fx.reverbMix = 22;
    s.fx.reverbSize = 70;
    s.mod.lfo[0] = Lfo{8, Lfo::kSine, 96, 35};   // SID2 cutoff, slow
    s.mod.pan[1] = -40;
    s.mod.pan[2] = 30;
    s.mod.pan[4] = 60;
    s.mod.pan[5] = -60;
    s.drumVolume = 85;
    const Chord prog[4] = {{9, true}, {9, true}, {5, false}, {7, false}};   // Am Am F G
    const int acid[16] = {0, -1, 12, 0, -1, 0, 12, -1, 0, 3, -1, 0, 12, -1, 10, 7};
    const Ev mel[] = {{0, 5, 4}, {3, 5, 7}, {6, 5, 9}, {10, 5, 7}, {12, 5, 4}, {16, 5, 0}, {22, 5, 2}, {28, 4, 11},
                      {32, 5, 0}, {35, 5, 4}, {38, 5, 5}, {42, 5, 4}, {44, 5, 0}, {48, 5, 2}, {54, 4, 11}, {58, 5, 2}};
    for (int p = 0; p < 4; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            // Drum column: four on the floor, clap on 4 / 12, hats between.
            for (int r = 0; r < 16; ++r) {
                uint8_t d = 0;
                if (r % 4 == 0) d = 1;
                else if (r % 2 == 0) d = r == 14 ? 5 : 4;
                if ((r == 4 || r == 12) && p >= 1) d = 3;
                pt.drum[r0 + r] = d;
            }
            for (int r = 0; r < 16; ++r)
                if (acid[r] >= 0)
                    put(pt, r0 + r, 0, nt(2, c.root + acid[r]), k.acid, r % 3 == 2 ? cmd::kTonePorta : 0,
                        r % 3 == 2 ? 4 : 0);
            if (p >= 1) {
                put(pt, r0 + 2, 1, nt(4, c.root), k.stab);
                put(pt, r0 + 10, 1, nt(4, c.root), k.stab);
                put(pt, r0, 3, nt(3, c.root + 7), k.pad);
                put(pt, r0 + 4, 4, nt(4, c.root), c.minor ? k.arpMin : k.arpMaj);
                put(pt, r0 + 12, 4, note::kKeyOff, 0);
            }
        }
        if (p >= 2)
            for (const Ev& e : mel) {
                put(pt, e.row, 2, nt(e.oct, e.semi), k.lead);
                put(pt, e.row + 2, 5, nt(e.oct + 1, e.semi), k.echo);
            }
    }
    order(s, {0, 1, 2, 3, 2, 3, 1}, 1);
    return save(s, dir + "/PCMGROOVE.sidt");
}

// --- FILTERS: two SIDs, slow sweeps, 100 bpm ---------------------------------
bool filters(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "FILTER STUDY");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.bpm = 100;
    s.speed = 6;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k8580;
    s.sid.balance = 80;
    const Kit k = buildKit(s, 0x01, 0x01);
    const Chord prog[4] = {{0, false}, {9, true}, {5, false}, {7, false}};   // C Am F G
    const Ev mel[] = {{0, 5, 7}, {8, 5, 4}, {16, 5, 9}, {24, 5, 7}, {32, 5, 5}, {40, 5, 9}, {48, 5, 7}, {56, 5, 2}};
    for (int p = 0; p < 3; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            put(pt, r0, 0, nt(2, c.root), k.bass);
            put(pt, r0 + 10, 0, nt(2, c.root + 7), k.bass);
            put(pt, r0, 3, nt(3, c.root), k.sweep);
            put(pt, r0, 4, nt(3, c.root + (c.minor ? 3 : 4)), k.pad);
            if (p >= 1) put(pt, r0 + 4, 1, nt(4, c.root), c.minor ? k.arpMin : k.arpMaj);
            if (p >= 1) put(pt, r0 + 6, 5, nt(6, c.root), k.bell);
        }
        if (p == 2)
            for (const Ev& e : mel) put(pt, e.row, 2, nt(e.oct, e.semi), k.slideLead, cmd::kVibrato, 2);
    }
    order(s, {0, 1, 2, 2, 1}, 1);
    return save(s, dir + "/FILTERS.sidt");
}

// --- CHIPMARCH: one SID, swing, commands -----------------------------------
bool chipMarch(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "CHIP MARCH");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.speed = 4;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.balance = 0;
    const Kit k = buildKit(s, 0x02, 0x02);
    const Chord prog[4] = {{7, false}, {0, false}, {2, false}, {7, false}};   // G C D G
    const Ev mel[] = {{0, 5, 7}, {2, 5, 11}, {4, 6, 2}, {6, 5, 11}, {8, 6, 0, cmd::kPortaUp, 3},
                      {12, 5, 9}, {16, 6, 4}, {20, 6, 0}, {24, 5, 9, cmd::kTonePorta, 4}, {28, 5, 7},
                      {32, 5, 6}, {34, 5, 9}, {36, 6, 2}, {40, 6, 6, cmd::kVibrato, 2}, {48, 6, 7},
                      {52, 6, 2, cmd::kPortaDown, 3}, {56, 5, 11}, {60, 5, 7}};
    for (int p = 0; p < 3; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        if (p == 0) put(pt, 0, 1, 0, 0, cmd::kFunkTempo, 5);   // swing: 4 / 3 ticks per row
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            for (int r = 0; r < 16; r += 4) {
                put(pt, r0 + r, 0, nt(2, c.root), k.bass);
                put(pt, r0 + r + 2, 0, nt(2, c.root + 7), k.bass);
            }
            put(pt, r0 + 4, 0, nt(4, 0), k.snare);
            put(pt, r0 + 12, 0, nt(4, 0), k.snare);
            if (p == 0 && bar == 0) put(pt, r0 + 2, 1, nt(4, c.root), k.arpMaj);
            else put(pt, r0, 1, nt(4, c.root), k.arpMaj);
            put(pt, r0 + 8, 1, nt(4, c.root), k.stab);
        }
        if (p >= 1)
            for (const Ev& e : mel) put(pt, e.row, 2, nt(e.oct, e.semi), k.slideLead, e.cmd, e.param);
    }
    order(s, {0, 1, 2, 1, 2}, 1);
    return save(s, dir + "/CHIPMARCH.sidt");
}

// --- Multispeed kit ----------------------------------------------------------
// Tables written for `m` ticks per frame: sweeps, vibrato and portamento keep
// their range and pace in frames but move in m times finer steps; the chord
// arpeggio steps every tick (m x 50 Hz, the multispeed sound); drums get
// finer pitch drops; hard restart stays two frames.
struct MultiKit {
    uint8_t bass = 1, fastMin = 2, fastMaj = 3, lead = 4, kick = 5, snare = 6, hat = 7, pad = 8, echo = 9,
            acid = 10, slowArp = 11;
    uint8_t vib = 0, slide = 0, slideFast = 0;   // speed table rows for commands 3 / 4
};

MultiKit buildMultiKit(Song& s, int m, uint8_t filterRoute) {
    MultiKit k;
    for (Table& t : s.tables) t = Table{};
    for (int i = 1; i < kInstrumentCount; ++i) s.inst[i].clear(i);
    TableWriter w{s.tables[kWaveTable]}, pu{s.tables[kPulseTable]}, fi{s.tables[kFilterTable]},
        sp{s.tables[kSpeedTable]};
    // Sweep lengths: 01-7F ticks (80 and up would be a "set" row).
    auto len = [m](int frames) { return uint8_t(frames * m > 0x7F ? 0x7F : frames * m); };
    // Vibrato: half cycle 4 frames, the same depth in m times smaller steps.
    k.vib = sp.add({{uint8_t(4 * m), uint8_t(0x18 / m)}});
    const uint8_t vibWide = sp.add({{uint8_t(3 * m), uint8_t(0x30 / m)}});
    k.slide = sp.add({{0x00, uint8_t(0x40 / m)}});
    k.slideFast = sp.add({{0x00, uint8_t(0xA0 / m)}});
    const uint8_t hold = uint8_t(m - 1);   // wait row: one frame per step
    const uint8_t wPulse = w.add({{0x41, 0x00}, {0xFF, 0x00}});
    // Chords stepping every tick.
    const uint8_t wFastMin = w.add({{0x41, 0x00}, {0x41, 0x03}, {0x41, 0x07}, {0xFF, 0x00}});
    w.loopTo(wFastMin);
    const uint8_t wFastMaj = w.add({{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0xFF, 0x00}});
    w.loopTo(wFastMaj);
    // The classic one-frame arpeggio for contrast: each note held a frame.
    const uint8_t wSlowArp = w.add({{0x41, 0x00}, {hold, 0x0C}, {hold, 0x07}, {hold, 0x03}, {hold, 0x00}, {0xFF, 0x00}});
    w.loopTo(uint8_t(wSlowArp + 1));
    const uint8_t wLead = w.add({{0x21, 0x0C}, {0x41, 0x00}, {0xFF, 0x00}});
    // Kick: a noise click, then the pitch falls 18 semitones in 4/m-semitone
    // steps, pulse into triangle.
    const uint8_t wKick = w.add({{0x81, 0xC4}, {0x41, 0xA8}});
    const int step = 4 / m > 0 ? 4 / m : 1;
    for (int n = 0xA8 - step; n > 0x96; n -= step) w.add({{uint8_t(n > 0xA0 ? 0x41 : 0x11), uint8_t(n)}});
    w.add({{0x10, 0x96}, {0xFF, 0x00}});
    const uint8_t wSnare = w.add({{0x81, 0xD8}, {0x41, 0xAA}, {0x81, 0xD4}, {hold ? hold : 0x80, 0xD2}, {0x80, 0xD0},
                                  {hold ? hold : 0x80, 0xCE}, {0x80, 0xCC}, {0xFF, 0x00}});
    const uint8_t wHat = w.add({{0x81, 0xDF}, {hold ? hold : 0x80, 0xDF}, {0x80, 0xDF}, {0xFF, 0x00}});
    const uint8_t wSaw = w.add({{0x21, 0x00}, {0xFF, 0x00}});
    const uint8_t wEcho = w.add({{0x41, 0x0C}, {0xFF, 0x00}});
    // Pulse sweeps: the same width range per frame, m times finer.
    const uint8_t pBass = pu.add({{0x82, 0x00}, {len(0x20), uint8_t(0x30 / m)}, {len(0x20), uint8_t(0x100 - 0x30 / m)},
                                  {0xFF, 0x02}});
    const uint8_t pChord = pu.add({{0x84, 0x00}, {len(0x40), uint8_t(0x10 / m)},
                                   {len(0x40), uint8_t(0x100 - 0x10 / m)}, {0xFF, 0x02}});
    const uint8_t pLead = pu.add({{0x86, 0x00}, {len(0x30), uint8_t(0x18 / m)},
                                  {len(0x30), uint8_t(0x100 - 0x18 / m)}, {0xFF, 0x02}});
    const uint8_t pDrum = pu.add({{0x88, 0x00}, {0xFF, 0x00}});
    const uint8_t pEcho = pu.add({{0x83, 0x00}, {0x7F, uint8_t(0x08 / m)}, {0xFF, 0x00}});
    // Filters: chords on voice 2, a pad sweep, an acid low pass on voice 1.
    const uint8_t fChord = fi.add({{0x90, uint8_t(0xA0 | filterRoute)}, {0x00, 0x28}, {len(0x20), 0x02},
                                   {len(0x20), 0xFE}, {0xFF, 0x03}});
    const uint8_t fPad = fi.add({{0xA0, uint8_t(0xC0 | filterRoute)}, {0x00, 0x10}, {0x7F, 0x01}, {0x7F, 0xFF},
                                 {0xFF, 0x0B}});
    // Acid: down from $70 by 8 per frame for 12 frames, in m steps per frame.
    const uint8_t fAcid = fi.add({{0x90, 0xF1}, {0x00, 0x70}, {len(0x0C), uint8_t(0x100 - 8 / m)}, {0x10, 0x00},
                                  {0xFF, 0x00}});
    (void)fPad;
    const uint8_t gate = uint8_t(2 * m);   // hard restart two frames ahead
    inst(s, k.bass, "PULSE BASS", 0x0A, 0x8A, wPulse, pBass, 0, 0, 0, gate);
    inst(s, k.fastMin, "FAST ARP MIN", 0x00, 0xA8, wFastMin, pChord, fChord, 0, 0, gate);
    inst(s, k.fastMaj, "FAST ARP MAJ", 0x00, 0xA8, wFastMaj, pChord, fChord, 0, 0, gate);
    inst(s, k.lead, "SMOOTH LEAD", 0x0A, 0xAA, wLead, pLead, 0, k.vib, uint8_t(10 * m), gate);
    inst(s, k.kick, "FINE KICK", 0x00, 0xE9, wKick, pDrum, 0, 0, 0, 0);
    inst(s, k.snare, "SNARE", 0x00, 0xF9, wSnare, pDrum, 0, 0, 0, 0);
    inst(s, k.hat, "HIHAT", 0x00, 0x00, wHat, 0, 0, 0, 0, 0);
    inst(s, k.pad, "SAW PAD", 0x8A, 0xCB, wSaw, 0, fPad, vibWide, uint8_t(30 * m), uint8_t(0x40 | gate));
    inst(s, k.echo, "ECHO LEAD", 0x2A, 0x6A, wEcho, pEcho, 0, k.vib, uint8_t(12 * m), gate);
    inst(s, k.acid, "ACID BASS", 0x00, 0xA9, wSaw, 0, fAcid, 0, 0, gate);
    inst(s, k.slowArp, "FRAME ARP", 0x00, 0xA8, wSlowArp, pChord, 0, 0, 0, gate);
    return k;
}

// --- HYPERARP: one SID, multispeed x4 ----------------------------------------
bool hyperArp(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "HYPER ARP X4");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    const int m = 4;
    s.multi = uint8_t(m);
    s.speed = uint8_t(6 * m);   // the row pace of speed 6
    s.sid.model[0] = sid::Model::k6581;
    s.sid.balance = 0;
    const MultiKit k = buildMultiKit(s, m, 0x02);
    const Chord prog[4] = {{9, true}, {5, false}, {0, false}, {7, false}};   // Am F C G
    const Ev mel[] = {{0, 5, 4},  {4, 5, 9},  {8, 5, 7},  {12, 5, 4}, {16, 5, 5}, {20, 5, 9},
                      {24, 6, 0, cmd::kTonePorta, 0}, {28, 5, 9}, {32, 5, 7}, {36, 5, 4},  {40, 5, 7},
                      {44, 6, 0}, {48, 5, 11}, {52, 5, 7, cmd::kTonePorta, 0}, {56, 5, 2}, {60, 5, 4}};
    for (int p = 0; p < 4; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            for (int r = 0; r < 16; r += 2) {
                if (r == 0 || r == 8 || (r == 14 && bar == 3)) put(pt, r0 + r, 0, nt(3, 0), k.kick);
                else if ((r == 4 || r == 12) && p >= 1) put(pt, r0 + r, 0, nt(4, 0), k.snare);
                else put(pt, r0 + r, 0, nt(r % 4 ? 3 : 2, c.root), k.bass);
            }
            // Track 2: the 200 Hz chord; pattern 3 swaps to the frame arpeggio.
            const uint8_t arp = p == 3 ? k.slowArp : (c.minor ? k.fastMin : k.fastMaj);
            put(pt, r0, 1, nt(4, c.root), arp);
            put(pt, r0 + 6, 1, nt(4, c.root), arp);
            put(pt, r0 + 12, 1, nt(4, c.root), arp);
        }
        if (p >= 1)
            for (const Ev& e : mel) {
                const uint8_t par = e.cmd == cmd::kTonePorta ? k.slide : e.param;
                put(pt, e.row, 2, nt(e.oct + (p == 2 ? 1 : 0), e.semi), p == 2 ? k.echo : k.lead, e.cmd, par);
            }
    }
    order(s, {0, 1, 2, 3, 1, 2}, 1);
    return save(s, dir + "/HYPERARP.sidt");
}

// --- SILKSWEEP: two SIDs, multispeed x2 --------------------------------------
bool silkSweep(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "SILK SWEEP X2");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    const int m = 2;
    s.multi = uint8_t(m);
    s.speed = uint8_t(6 * m);
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k8580;
    s.sid.balance = 70;
    const MultiKit k = buildMultiKit(s, m, 0x02);
    const Chord prog[4] = {{2, true}, {10, false}, {0, false}, {9, true}};   // Dm Bb C Am
    const int acid[16] = {0, -1, 12, 0, -1, 0, 12, -1, 0, 3, -1, 0, 12, -1, 10, 7};
    const Ev mel[] = {{0, 5, 2},  {6, 5, 5},  {8, 5, 9},  {12, 5, 7, cmd::kTonePorta, 0}, {16, 5, 5},
                      {22, 5, 2}, {24, 5, 10}, {28, 5, 9, cmd::kTonePorta, 0}, {32, 5, 7}, {38, 5, 4},
                      {40, 5, 0}, {44, 5, 4}, {48, 5, 9}, {52, 5, 12, cmd::kTonePorta, 0}, {56, 5, 9}, {60, 5, 4}};
    for (int p = 0; p < 4; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            // SID1: acid bass, drums, lead.
            for (int r = 0; r < 16; ++r)
                if (acid[r] >= 0)
                    put(pt, r0 + r, 0, nt(2, c.root + acid[r]), k.acid, r % 3 == 2 ? cmd::kTonePorta : 0,
                        r % 3 == 2 ? k.slideFast : 0);
            for (int r = 0; r < 16; r += 2) {
                uint8_t d = k.hat;
                if (r == 0 || r == 8) d = k.kick;
                else if ((r == 4 || r == 12) && p >= 1) d = k.snare;
                put(pt, r0 + r, 1, nt(d == k.snare ? 4 : 3, 0), d);
            }
            // SID2: pad, chords, the echo of the lead.
            put(pt, r0, 3, nt(3, c.root + 7), k.pad);
            if (p >= 1) {
                put(pt, r0 + 4, 4, nt(4, c.root), c.minor ? k.fastMin : k.fastMaj);
                put(pt, r0 + 12, 4, note::kKeyOff, 0);
            }
        }
        if (p >= 2)
            for (const Ev& e : mel) {
                const uint8_t par = e.cmd == cmd::kTonePorta ? k.slide : e.param;
                put(pt, e.row, 2, nt(e.oct, e.semi), k.lead, e.cmd, par);
                put(pt, e.row + 3, 5, nt(e.oct + 1, e.semi), k.echo, e.cmd, par);
            }
    }
    order(s, {0, 1, 2, 3, 2, 3, 1}, 1);
    return save(s, dir + "/SILKSWEEP.sidt");
}

// --- Demoscene songs from the instrument library ------------------------------
// The instruments come from the SOUNDS presets and the Classic stock
// instruments (the same programs as /SIDTracker/Instruments on the card).

struct Lib {
    Song& s;
    // Loads `name` into `slot`; `classicFirst` picks the Classic version of
    // names both libraries have. `voice` = the SID voice its filter routes.
    uint8_t load(uint8_t slot, const char* name, int voice = 0, bool classicFirst = false) {
        const library::Preset* p = nullptr;
        auto findClassic = [&]() {
            for (const library::Preset& c : classic::kClassic)
                if (!std::strcmp(c.name, name)) p = &c;
        };
        if (classicFirst) findClassic();
        for (int i = 0; !p && i < library::presetCount(); ++i)
            if (!std::strcmp(library::preset(i).name, name)) p = &library::preset(i);
        if (!p) findClassic();
        if (!p || library::load(s, slot, *p, voice) != library::Result::kOk) {
            std::fprintf(stderr, "instrument %s not loaded\n", name);
            return 0;
        }
        return slot;
    }
    // A speed-table row for commands 1-4 (portamento speed / vibrato).
    uint8_t speed(uint8_t l, uint8_t r) {
        Table& t = s.tables[kSpeedTable];
        const int row = t.used() + 1;
        t.left[row - 1] = l;
        t.right[row - 1] = r;
        return uint8_t(row);
    }
};

void freshInstruments(Song& s) {
    for (Table& t : s.tables) t = Table{};
    for (int i = 1; i < kInstrumentCount; ++i) s.inst[i].clear(i);
}

// --- CRACKINTRO: one SID, A minor, the classic cracktro shape -----------------
bool crackIntro(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "CRACK INTRO");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.speed = 6;
    s.sid.model[0] = sid::Model::k6581;
    s.sid.balance = 0;
    freshInstruments(s);
    Lib L{s};
    const uint8_t bass = L.load(1, "PULSE BASS"), kick = L.load(2, "KICK"), snare = L.load(3, "SNARE"),
                  minArp = L.load(4, "MINOR ARP"), majArp = L.load(5, "MAJOR ARP"), lead = L.load(6, "SAW LEAD"),
                  echo = L.load(7, "ECHO LEAD"), hat = L.load(8, "HIHAT"), laser = L.load(9, "LASER");
    const uint8_t slide = L.speed(0x00, 0x60);   // tone portamento speed
    // Am  F  G  Em, a bar of 16 rows each.
    const Chord prog[4] = {{9, true}, {5, false}, {7, false}, {4, true}};
    const Ev melA[] = {{0, 5, 4},  {6, 5, 9},  {8, 5, 7},  {10, 5, 4}, {12, 5, 2},  {14, 5, 4},
                       {16, 5, 0}, {22, 4, 9}, {24, 5, 0}, {26, 5, 2}, {28, 5, 0},  {30, 4, 9},
                       {32, 4, 11}, {36, 5, 2}, {40, 5, 7}, {44, 5, 9, cmd::kTonePorta, 1}, {46, 5, 7},
                       {48, 5, 7}, {52, 5, 4}, {56, 5, 2}, {58, 5, 4}};
    const Ev melB[] = {{0, 5, 9},  {4, 6, 0},  {6, 5, 11}, {8, 5, 9},  {10, 5, 7}, {12, 5, 4},
                       {16, 5, 5}, {20, 5, 4}, {22, 5, 5}, {24, 5, 9}, {28, 5, 7}, {30, 5, 5},
                       {32, 5, 7}, {36, 5, 2}, {38, 5, 7}, {40, 5, 11}, {44, 5, 9}, {46, 5, 7},
                       {48, 5, 4}, {56, 5, 7, cmd::kTonePorta, 1}, {60, 5, 4}};
    // Patterns: 0 drums + bass, 1 + arps, 2 + melody A, 3 + melody B, 4 break (arps + echo, no drums).
    for (int p = 0; p < 5; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            // Track 1: the drums woven into the bass line (one voice does both).
            for (int r = 0; r < 16; r += 2) {
                if (p == 4) {   // break: bass only, longer notes
                    if (r % 8 == 0) put(pt, r0 + r, 0, nt(2, c.root), bass);
                    continue;
                }
                if (r == 0 || (r == 10 && bar % 2 == 1)) put(pt, r0 + r, 0, nt(3, 0), kick);
                else if (r == 4 || r == 12) put(pt, r0 + r, 0, nt(4, 0), p == 0 && r == 4 ? hat : snare);
                else put(pt, r0 + r, 0, nt(r == 6 || r == 14 ? 3 : 2, c.root), bass);
            }
            // Track 2: chord arpeggios, re-struck every half bar.
            if (p >= 1) {
                put(pt, r0, 1, nt(4, c.root), c.minor ? minArp : majArp);
                put(pt, r0 + 8, 1, nt(4, c.root), c.minor ? minArp : majArp);
            }
        }
        if (p == 2 || p == 3) {
            const Ev* mel = p == 2 ? melA : melB;
            const int n = p == 2 ? int(sizeof melA / sizeof melA[0]) : int(sizeof melB / sizeof melB[0]);
            for (int i = 0; i < n; ++i) {
                const Ev& e = mel[i];
                if (e.cmd == cmd::kTonePorta) put(pt, e.row, 2, nt(e.oct, e.semi), 0, cmd::kTonePorta, slide);
                else put(pt, e.row, 2, nt(e.oct, e.semi), lead);
            }
        }
        if (p == 4)   // the break: the echo lead answers itself an octave up
            for (const Ev& e : melA) {
                if (e.row % 4) continue;
                put(pt, e.row, 2, nt(e.oct + 1, e.semi), echo);
            }
        if (p == 0) put(pt, 60, 2, nt(5, 0), laser);   // a laser into the arps
    }
    order(s, {0, 1, 2, 3, 2, 3, 4, 1}, 1);
    return save(s, dir + "/CRACKINTRO.sidt");
}

// --- RASTERBARS: two SIDs, D minor, the driving main part ---------------------
bool rasterBars(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "RASTER BARS");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.speed = 5;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k8580;
    s.sid.balance = 60;
    freshInstruments(s);
    Lib L{s};
    // SID 1: wobble bass (its filter), drums, saw lead. SID 2: power chords,
    // arps, the screaming echo lead (its filter, voice 3 of SID 2).
    const uint8_t wob = L.load(1, "WOBBLE BASS", 0, true), kick = L.load(2, "KICK 2", 0, true),
                  snare = L.load(3, "SNARE"), hat = L.load(4, "HIHAT"), lead = L.load(5, "SAW LEAD"),
                  power = L.load(6, "POWER CHORD", 0, true), minArp = L.load(7, "MINOR ARP"),
                  majArp = L.load(8, "MAJOR ARP"), scream = L.load(9, "SCREAM LEAD", 2),
                  clap = L.load(10, "CLAP", 0, true), laser = L.load(11, "LASER", 0, true),
                  openHat = L.load(12, "OPEN HAT", 0, true);
    const uint8_t slide = L.speed(0x00, 0x80);
    const uint8_t vib = L.speed(0x03, 0x30);
    // Dm  Bb  C  A
    const Chord prog[4] = {{2, true}, {10, false}, {0, false}, {9, false}};
    const int bassLine[16] = {0, -1, 0, 12, -1, 0, 12, 0, 0, -1, 0, 12, -1, 12, 10, 7};
    const Ev melA[] = {{0, 5, 2},  {3, 5, 5},  {6, 5, 9},  {8, 5, 7},  {10, 5, 5},  {12, 5, 4},
                       {16, 5, 5}, {19, 5, 2}, {22, 5, 5}, {24, 5, 10}, {28, 5, 9},
                       {32, 5, 7}, {35, 5, 4}, {38, 5, 7}, {40, 6, 0, cmd::kTonePorta, 1}, {44, 5, 10},
                       {48, 5, 9}, {52, 5, 1}, {56, 5, 4}, {60, 5, 9, cmd::kVibrato, 1}};
    const Ev melB[] = {{0, 5, 9},  {2, 5, 7},  {4, 5, 5},  {6, 5, 2},  {8, 5, 5},  {12, 5, 9},
                       {16, 5, 10}, {20, 6, 2}, {24, 6, 0}, {28, 5, 10},
                       {32, 6, 0}, {34, 5, 10}, {36, 5, 9}, {38, 5, 7}, {40, 5, 4}, {44, 5, 7},
                       {48, 5, 9}, {56, 6, 1, cmd::kTonePorta, 1}, {60, 6, 2}};
    // 0 intro (bass, kick, laser), 1 + drums + chords, 2 + lead A, 3 + lead B,
    // 4 breakdown (arps, scream, claps), 5 drum fill + lead A.
    for (int p = 0; p < 6; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            for (int r = 0; r < 16; ++r)
                if (bassLine[r] >= 0 && p != 4) put(pt, r0 + r, 0, nt(2, c.root + bassLine[r]), wob);
            if (p == 4 && (bar % 2 == 0)) put(pt, r0, 0, nt(2, c.root), wob);
            // Track 2: drums.
            for (int r = 0; r < 16; ++r) {
                uint8_t d = 0;
                if (p == 0) d = r % 4 == 0 ? kick : 0;
                else if (p == 4) d = (r == 4 || r == 12) ? clap : 0;
                else if (r % 4 == 0) d = (r == 4 || r == 12) ? snare : kick;
                else if (r % 2 == 0) d = r == 14 ? openHat : hat;
                if (p == 5 && bar == 3 && r >= 8) d = r % 2 ? 0 : snare;   // fill
                if (d) put(pt, r0 + r, 1, nt(d == snare || d == clap ? 4 : 3, 0), d);
            }
            // SID 2: power chords on the off-beats, arps in the breakdown.
            if (p >= 1 && p != 4)
                for (int r : {0, 3, 6, 10}) put(pt, r0 + r, 3, nt(3, c.root), power);
            if (p >= 3 || p == 1) put(pt, r0, 4, nt(4, c.root), c.minor ? minArp : majArp);
        }
        const Ev* mel = nullptr;
        int n = 0;
        if (p == 2 || p == 5) mel = melA, n = int(sizeof melA / sizeof melA[0]);
        if (p == 3) mel = melB, n = int(sizeof melB / sizeof melB[0]);
        for (int i = 0; i < n; ++i) {
            const Ev& e = mel[i];
            const uint8_t prm = e.cmd == cmd::kTonePorta ? slide : (e.cmd == cmd::kVibrato ? vib : 0);
            put(pt, e.row, 2, nt(e.oct, e.semi), e.cmd == cmd::kTonePorta ? 0 : lead, e.cmd, prm);
            // The screaming echo two rows later an octave up (SID 2).
            if (p == 3) put(pt, e.row + 2, 5, nt(e.oct + 1, e.semi), scream);
        }
        if (p == 4)
            for (const Ev& e : melB) put(pt, e.row, 5, nt(e.oct, e.semi), scream);
        if (p == 0) {
            put(pt, 0, 2, nt(6, 0), laser);
            put(pt, 32, 2, nt(6, 0), laser);
        }
    }
    order(s, {0, 1, 2, 3, 4, 5, 3}, 1);
    return save(s, dir + "/RASTERBARS.sidt");
}

// --- ENDSCROLL: two SIDs, C major, slow and wide ------------------------------
bool endScroll(const std::string& dir) {
    Box b;
    Song& s = *b.song;
    std::snprintf(s.name, sizeof s.name, "END SCROLL");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.speed = 8;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k8580;
    s.sid.balance = 80;
    freshInstruments(s);
    Lib L{s};
    // SID 1: bass, space pad (its filter, voice 2), flute. SID 2: strings
    // (its filter, voice 1), marimba, ring bell (rings with the marimba).
    const uint8_t bass = L.load(1, "TRI BASS"), pad = L.load(2, "SPACE PAD", 1, true),
                  flute = L.load(3, "FLUTE", 0, true), strings = L.load(4, "STRINGS", 0),
                  marimba = L.load(5, "MARIMBA"), bell = L.load(6, "RING BELL");
    const uint8_t glide = L.speed(0x00, 0x30);
    // C  Am  F  G
    const Chord prog[4] = {{0, false}, {9, true}, {5, false}, {7, false}};
    const int broken[8] = {0, 7, 12, 16, 12, 7, 4, 7};   // marimba: a broken chord
    const Ev melA[] = {{0, 5, 7},  {8, 5, 4},  {12, 5, 2},  {16, 5, 0}, {24, 4, 9}, {28, 5, 0},
                       {32, 5, 5}, {40, 5, 9, cmd::kTonePorta, 1}, {44, 5, 7}, {48, 5, 2}, {56, 5, 7}};
    const Ev melB[] = {{0, 6, 0},  {6, 5, 11}, {8, 5, 7},  {12, 5, 4}, {16, 5, 9}, {22, 5, 7},
                       {24, 5, 4}, {28, 5, 0}, {32, 5, 5}, {36, 5, 9}, {40, 6, 0}, {44, 5, 9},
                       {48, 5, 11, cmd::kTonePorta, 1}, {56, 6, 0}};
    // 0 pads + bass, 1 + marimba, 2 + flute A, 3 + flute B + bells.
    for (int p = 0; p < 4; ++p) {
        Pattern& pt = s.pattern(p);
        pt.clear(64);
        for (int bar = 0; bar < 4; ++bar) {
            const Chord& c = prog[bar];
            const int r0 = bar * 16;
            put(pt, r0, 0, nt(2, c.root), bass);
            put(pt, r0 + 10, 0, nt(2, c.root + 7), bass);
            put(pt, r0, 1, nt(3, c.root), pad);
            put(pt, r0, 3, nt(4, c.root + (c.minor ? 3 : 4)), strings);
            if (p >= 1)
                for (int i = 0; i < 8; ++i) put(pt, r0 + i * 2, 4, nt(4, c.root + broken[i]), marimba);
            if (p == 3) {
                put(pt, r0 + 4, 5, nt(5, c.root + 7), bell);
                put(pt, r0 + 12, 5, nt(6, c.root), bell);
            }
        }
        if (p >= 2) {
            const Ev* mel = p == 2 ? melA : melB;
            const int n = p == 2 ? int(sizeof melA / sizeof melA[0]) : int(sizeof melB / sizeof melB[0]);
            for (int i = 0; i < n; ++i) {
                const Ev& e = mel[i];
                if (e.cmd == cmd::kTonePorta) put(pt, e.row, 2, nt(e.oct, e.semi), 0, cmd::kTonePorta, glide);
                else put(pt, e.row, 2, nt(e.oct, e.semi), flute);
            }
        }
    }
    order(s, {0, 1, 2, 3, 2, 3, 1}, 1);
    return save(s, dir + "/ENDSCROLL.sidt");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_examples OUTDIR\n");
        return 2;
    }
    const std::string dir = argv[1];
    return nightDrive(dir) && pcmGroove(dir) && filters(dir) && chipMarch(dir) && hyperArp(dir) && silkSweep(dir) && crackIntro(dir) &&
                   rasterBars(dir) && endScroll(dir)
               ? 0
               : 1;
}
