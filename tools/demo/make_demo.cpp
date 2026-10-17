// make_demo -- writes DUALDEMO.sidt, an original SIDTracker demo song that
// uses both SIDs: SID1 (8580) bass, filtered chord arpeggios and a lead;
// SID2 (6581) drums, a resonant swept pad and an echo of the lead, the two
// chips spread in stereo. Am - F - C - G, 125 BPM, speed 6.
//   make_demo OUT.sidt
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <memory>

#include "tracker/commands/commands.h"
#include "tracker/song/project_file.h"

using namespace sidt;

namespace {

struct FileWriter : project::Writer {
    std::FILE* f;
    bool write(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};

constexpr int kRowsPerBar = 16;
// Tracks: 0 bass, 1 chords, 2 lead (SID1); 3 drums, 4 pad, 5 echo (SID2).
enum Inst : uint8_t { kBass = 1, kMinor, kMajor, kLead, kKick, kSnare, kHat, kPad, kEcho };

uint8_t note(int octave, int semitone) { return uint8_t(1 + octave * 12 + semitone); }

// Table rows are written in order; each instrument points at its first row.
struct TableWriter {
    Table& t;
    int row = 0;   // next free index (row number - 1)
    uint8_t add(std::initializer_list<std::pair<uint8_t, uint8_t>> rows) {
        const uint8_t start = uint8_t(row + 1);
        for (auto& r : rows) {
            t.left[row] = r.first;
            t.right[row] = r.second;
            ++row;
        }
        return start;
    }
};

void instrument(Song& s, uint8_t i, const char* name, uint8_t ad, uint8_t sr, uint8_t wave, uint8_t pulse,
                uint8_t filter, uint8_t vib = 0, uint8_t vibDelay = 0, uint8_t gate = 2) {
    Instrument& in = s.inst[i];
    std::snprintf(in.name, sizeof in.name, "%s", name);
    in.ad = ad;
    in.sr = sr;
    in.firstWave = 0x09;
    in.wavePtr = wave;
    in.pulsePtr = pulse;
    in.filterPtr = filter;
    in.vibParam = vib;
    in.vibDelay = vibDelay;
    in.gateTimer = gate;
}

struct Chord {
    int root;    // semitone 0..11
    bool minor;
};
const Chord kChords[4] = {{9, true}, {5, false}, {0, false}, {7, false}};   // Am F C G

// Lead melody over the four bars: (row in the 64-row phrase, octave, semitone).
struct Note {
    int row, oct, semi;
};
const Note kMelody[] = {
    {0, 5, 4},   {6, 5, 0},  {8, 5, 2},  {10, 5, 4}, {14, 4, 9},              // Am: E C D E A
    {16, 5, 5},  {20, 5, 4}, {22, 5, 0}, {26, 4, 9}, {30, 5, 0},              // F:  F E C A C
    {32, 5, 7},  {38, 5, 4}, {40, 5, 5}, {42, 5, 7}, {46, 6, 0},              // C:  G E F G C
    {48, 5, 11}, {52, 5, 9}, {54, 5, 7}, {58, 5, 2}, {62, 5, 4},              // G:  B A G D E
};

void bassBar(Pattern& p, int bar, const Chord& c) {
    const int r0 = bar * kRowsPerBar;
    const int rows[] = {0, 4, 6, 8, 11, 12, 14};
    for (int r : rows) {
        const int oct = (r == 6 || r == 14) ? 3 : 2;
        p.cells[r0 + r][0] = {note(oct, c.root), kBass, 0, 0};
    }
}

void chordBar(Pattern& p, int bar, const Chord& c) {
    const int r0 = bar * kRowsPerBar;
    for (int r = 0; r < kRowsPerBar; r += 8) p.cells[r0 + r][1] = {note(4, c.root), uint8_t(c.minor ? kMinor : kMajor), 0, 0};
    p.cells[r0 + 6][1] = {note::kKeyOff, 0, 0, 0};
    p.cells[r0 + 14][1] = {note::kKeyOff, 0, 0, 0};
}

void drumBar(Pattern& p, int bar, bool fill) {
    const int r0 = bar * kRowsPerBar;
    for (int r = 0; r < kRowsPerBar; r += 2) p.cells[r0 + r][3] = {note(5, 0), kHat, 0, 0};
    p.cells[r0 + 0][3] = {note(3, 0), kKick, 0, 0};
    p.cells[r0 + 8][3] = {note(3, 0), kKick, 0, 0};
    p.cells[r0 + 10][3] = {note(3, 0), kKick, 0, 0};
    p.cells[r0 + 4][3] = {note(4, 0), kSnare, 0, 0};
    p.cells[r0 + 12][3] = {note(4, 0), kSnare, 0, 0};
    if (fill) {
        p.cells[r0 + 14][3] = {note(4, 0), kSnare, 0, 0};
        p.cells[r0 + 15][3] = {note(4, 0), kSnare, 0, 0};
    }
}

void padBar(Pattern& p, int bar, const Chord& c) {
    p.cells[bar * kRowsPerBar][4] = {note(3, (c.root + 7) % 12), kPad, 0, 0};
}

// Lead (track 2) and its echo three rows later, an octave up (track 5).
void leadPhrase(Pattern& p, int half) {
    for (const Note& n : kMelody) {
        const int r = n.row - half * 32;
        if (r < 0 || r >= 32) continue;
        p.cells[r][2] = {note(n.oct, n.semi), kLead, 0, 0};
        if (r + 3 < 32) p.cells[r + 3][5] = {note(n.oct, n.semi), kEcho, 0, 0};
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_demo OUT.sidt\n");
        return 2;
    }
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> sp(new Song);
    Song& s = *sp;
    s.patterns = pats.get();
    s.clear();
    std::snprintf(s.name, sizeof s.name, "DUAL SID DEMO");
    std::snprintf(s.author, sizeof s.author, "SIDTRACKER");
    s.bpm = 125;
    s.speed = 6;
    s.sid.model[0] = sid::Model::k8580;
    s.sid.model[1] = sid::Model::k6581;
    s.sid.balance = 60;
    for (Table& t : s.tables) t = Table{};
    for (int i = 1; i < kInstrumentCount; ++i) s.inst[i].clear(i);

    TableWriter w{s.tables[kWaveTable]}, pu{s.tables[kPulseTable]}, fi{s.tables[kFilterTable]},
        sp_{s.tables[kSpeedTable]};
    const uint8_t vib = sp_.add({{0x04, 0x18}});   // vibrato: speed 4, depth 0x18
    // Wave tables.
    const uint8_t wPulse = w.add({{0x41, 0x00}, {0xFF, 0x00}});
    const uint8_t wMinor = w.add({{0x41, 0x00}, {0x41, 0x03}, {0x41, 0x07}, {0x41, 0x0C}, {0xFF, 0x00}});
    // Arpeggios loop over their three notes: patch the jumps.
    s.tables[kWaveTable].left[wMinor + 2] = 0xFF;
    s.tables[kWaveTable].right[wMinor + 2] = wMinor;
    const uint8_t wMajor = w.add({{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0xFF, 0x00}});
    s.tables[kWaveTable].right[wMajor + 2] = wMajor;
    const uint8_t wLead = w.add({{0x21, 0x0C}, {0x41, 0x00}, {0xFF, 0x00}});
    const uint8_t wKick = w.add({{0x81, 0xC4}, {0x41, 0xA8}, {0x41, 0xA2}, {0x11, 0x9E}, {0x11, 0x9A},
                                 {0x10, 0x96}, {0xFF, 0x00}});
    const uint8_t wSnare = w.add({{0x81, 0xD8}, {0x41, 0xAA}, {0x81, 0xD4}, {0x80, 0xD1}, {0x80, 0xCE},
                                  {0x80, 0xCC}, {0xFF, 0x00}});
    const uint8_t wHat = w.add({{0x81, 0xDF}, {0x80, 0xDF}, {0xFF, 0x00}});
    const uint8_t wPad = w.add({{0x21, 0x00}, {0xFF, 0x00}});
    const uint8_t wEcho = w.add({{0x41, 0x0C}, {0xFF, 0x00}});
    // Pulse tables.
    const uint8_t pBass = pu.add({{0x82, 0x00}, {0x20, 0x30}, {0x20, 0xD0}, {0xFF, 0x02}});
    const uint8_t pChord = pu.add({{0x84, 0x00}, {0x40, 0x10}, {0x40, 0xF0}, {0xFF, 0x02}});
    const uint8_t pLead = pu.add({{0x86, 0x00}, {0x30, 0x18}, {0x30, 0xE8}, {0xFF, 0x02}});
    const uint8_t pDrum = pu.add({{0x88, 0x00}, {0xFF, 0x00}});
    const uint8_t pEcho = pu.add({{0x83, 0x00}, {0x7F, 0x08}, {0xFF, 0x00}});
    // Filter tables: SID1 low pass on the chord voice (voice 2), SID2 band
    // pass sweep on the pad voice (voice 2 of SID2).
    const uint8_t fChord = fi.add({{0x90, 0xA2}, {0x00, 0x28}, {0x20, 0x02}, {0x20, 0xFE}, {0xFF, 0x03}});
    const uint8_t fPad = fi.add({{0xA0, 0xC2}, {0x00, 0x10}, {0x60, 0x01}, {0x60, 0xFF}, {0xFF, 0x03}});

    instrument(s, kBass, "BASS", 0x0A, 0x8A, wPulse, pBass, 0);
    instrument(s, kMinor, "ARP MINOR", 0x00, 0xA8, wMinor, pChord, fChord);
    instrument(s, kMajor, "ARP MAJOR", 0x00, 0xA8, wMajor, pChord, fChord);
    instrument(s, kLead, "LEAD", 0x0A, 0xAA, wLead, pLead, 0, vib, 10);
    instrument(s, kKick, "KICK", 0x00, 0xE9, wKick, pDrum, 0, 0, 0, 0);
    instrument(s, kSnare, "SNARE", 0x00, 0xF9, wSnare, pDrum, 0, 0, 0, 0);
    instrument(s, kHat, "HIHAT", 0x00, 0x00, wHat, 0, 0, 0, 0, 0);
    instrument(s, kPad, "PAD", 0x8A, 0xCB, wPad, 0, fPad, 0, 0, 0x42);
    instrument(s, kEcho, "ECHO", 0x2A, 0x6A, wEcho, pEcho, 0, vib, 12);

    // Patterns (32 rows = 2 bars): 0/1 intro (bass, drums), 2/3 + chords and
    // pad, 4/5 + lead and echo.
    for (int p = 0; p < 6; ++p) {
        Pattern& pat = s.pattern(p);
        pat.clear(32);
        const int pair = p & 1;   // 0: Am F, 1: C G
        for (int bar = 0; bar < 2; ++bar) {
            const Chord& c = kChords[pair * 2 + bar];
            bassBar(pat, bar, c);
            drumBar(pat, bar, bar == 1 && pair == 1);
            if (p >= 2) {
                chordBar(pat, bar, c);
                padBar(pat, bar, c);
            }
        }
        if (p >= 4) leadPhrase(pat, pair);
    }
    const uint8_t order[] = {0, 1, 2, 3, 4, 5, 4, 5, 2, 3};
    s.orderLen = sizeof order;
    for (int i = 0; i < s.orderLen; ++i) s.order[i] = {order[i], 0, 1};
    s.order[8].transpose = 0;
    s.restartPos = 2;

    FileWriter fw;
    fw.f = std::fopen(argv[1], "wb");
    if (!fw.f) return std::perror(argv[1]), 1;
    const bool ok = project::save(s, fw) == project::Result::kOk;
    std::fclose(fw.f);
    std::printf("%s: %s\n", argv[1], ok ? "written" : "write error");
    return ok ? 0 : 1;
}
