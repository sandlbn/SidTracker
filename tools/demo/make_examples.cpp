// make_examples -- writes SIDTracker's original example songs (MIT, made for
// the release zip; no third-party music):
//   NIGHTDRIVE  one SID (6581): drums woven into the bass track, arps, lead
//   PCMGROOVE   two SIDs + PCM drum column, delay, reverb, LFO, voice pan
//   FILTERS     two SIDs (8580): slow resonant filter sweeps, 100 bpm (CIA)
//   CHIPMARCH   one SID (8580): swing tempo, slides, tone portamento, vibrato
//   make_examples OUTDIR
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "tracker/commands/commands.h"
#include "tracker/song/project_file.h"

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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_examples OUTDIR\n");
        return 2;
    }
    const std::string dir = argv[1];
    return nightDrive(dir) && pcmGroove(dir) && filters(dir) && chipMarch(dir) ? 0 : 1;
}
