// SIDTracker -- sound library and arpeggiator insert (sound_library.h).
// Table row meanings: docs/commands.md.
#include "tracker/song/sound_library.h"

#include <cstdio>
#include <cstring>

namespace sidt {
namespace library {

namespace {

// clang-format off
// name, category, text, AD, SR, first wave, gate timer, vibrato speed / depth / delay,
// wave program, pulse program, filter program.
const Preset kPresets[] = {
    // --- bass ---------------------------------------------------------------
    {"PULSE BASS", kBass, "hard-restart pulse, width sweep", 0x0A, 0x8A, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0xFF, 0x00}}, {{0x82, 0x00}, {0x20, 0x30}, {0x20, 0xD0}, {0xFF, 0x02}}, {}},
    {"SAW BASS", kBass, "plain sawtooth", 0x09, 0x99, 0x09, 0x02, 0, 0, 0,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"SNAP BASS", kBass, "noise click, then pulse", 0x08, 0xA9, 0x09, 0x02, 0, 0, 0,
     {{0x81, 0x18}, {0x41, 0x00}, {0xFF, 0x00}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"OCTAVE BASS", kBass, "root / octave every tick", 0x0A, 0x8A, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x0C}, {0xFF, 0x01}}, {{0x83, 0x00}, {0x10, 0x20}, {0x10, 0xE0}, {0xFF, 0x02}}, {}},
    {"ACID BASS", kBass, "saw, resonant low pass closing", 0x00, 0xA9, 0x09, 0x02, 0, 0, 0,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {{0x90, 0xF0}, {0x00, 0x70}, {0x0C, 0xF8}, {0xFF, 0x00}}},
    {"TRI BASS", kBass, "soft triangle", 0x0A, 0xAA, 0x09, 0x02, 0, 0, 0,
     {{0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"DROP BASS", kBass, "falls an octave into the note", 0x09, 0x89, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x0C}, {0x41, 0x07}, {0x41, 0x00}, {0xFF, 0x00}}, {{0x86, 0x00}, {0xFF, 0x00}}, {}},
    {"SUB THUMP", kBass, "short low triangle hit", 0x06, 0x00, 0x09, 0x02, 0, 0, 0,
     {{0x11, 0x0C}, {0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    // --- lead ---------------------------------------------------------------
    {"PULSE LEAD", kLead, "saw attack, sweeping pulse, vibrato", 0x0A, 0xAA, 0x09, 0x02, 0x04, 0x18, 10,
     {{0x21, 0x0C}, {0x41, 0x00}, {0xFF, 0x00}}, {{0x86, 0x00}, {0x30, 0x18}, {0x30, 0xE8}, {0xFF, 0x02}}, {}},
    {"SAW LEAD", kLead, "sawtooth with delayed vibrato", 0x0A, 0xAA, 0x09, 0x02, 0x04, 0x20, 12,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"THIN LEAD", kLead, "narrow fast-moving pulse", 0x0A, 0xAA, 0x09, 0x02, 0x04, 0x18, 6,
     {{0x41, 0x00}, {0xFF, 0x00}}, {{0x81, 0x00}, {0x60, 0x04}, {0x60, 0xFC}, {0xFF, 0x02}}, {}},
    {"FLUTE", kLead, "triangle, soft attack, vibrato", 0x5A, 0xAA, 0x09, 0x02, 0x03, 0x10, 8,
     {{0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"CHIRP LEAD", kLead, "slides up three semitones", 0x0A, 0xAA, 0x09, 0x02, 0x04, 0x18, 12,
     {{0x41, 0x7D}, {0x41, 0x7E}, {0x41, 0x7F}, {0x41, 0x00}, {0xFF, 0x00}}, {{0x85, 0x00}, {0xFF, 0x00}}, {}},
    {"SQUARE LEAD", kLead, "50% pulse, vibrato", 0x0A, 0xAA, 0x09, 0x02, 0x04, 0x14, 16,
     {{0x41, 0x00}, {0xFF, 0x00}}, {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    {"SCREAM LEAD", kLead, "saw through a moving band pass", 0x0A, 0xBA, 0x09, 0x02, 0x04, 0x18, 16,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {{0xA0, 0xF0}, {0x00, 0x40}, {0x20, 0x02}, {0x20, 0xFE}, {0xFF, 0x03}}},
    {"ECHO LEAD", kLead, "octave up, slowly widening pulse", 0x2A, 0x6A, 0x09, 0x02, 0x04, 0x18, 12,
     {{0x41, 0x0C}, {0xFF, 0x00}}, {{0x83, 0x00}, {0x7F, 0x08}, {0xFF, 0x00}}, {}},
    // --- chords / arpeggios ---------------------------------------------------
    {"MAJOR ARP", kChord, "0 4 7, one note per tick", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0xFF, 0x01}}, {{0x84, 0x00}, {0x40, 0x10}, {0x40, 0xF0}, {0xFF, 0x02}}, {}},
    {"MINOR ARP", kChord, "0 3 7, one note per tick", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x03}, {0x41, 0x07}, {0xFF, 0x01}}, {{0x84, 0x00}, {0x40, 0x10}, {0x40, 0xF0}, {0xFF, 0x02}}, {}},
    {"MAJ7 ARP", kChord, "0 4 7 11", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x04}, {0x41, 0x07}, {0x41, 0x0B}, {0xFF, 0x01}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"MIN7 ARP", kChord, "0 3 7 10", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x03}, {0x41, 0x07}, {0x41, 0x0A}, {0xFF, 0x01}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"POWER ARP", kChord, "saw 0 7 12", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x21, 0x00}, {0x21, 0x07}, {0x21, 0x0C}, {0xFF, 0x01}}, {}, {}},
    {"CHORD STAB", kChord, "noise hit, then a short 0 7 12", 0x00, 0x89, 0x09, 0x02, 0, 0, 0,
     {{0x81, 0x30}, {0x41, 0x00}, {0x41, 0x07}, {0x41, 0x0C}, {0x40, 0x0C}, {0xFF, 0x00}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"SLOW MAJOR", kChord, "0 4 7, two ticks per note", 0x00, 0xA8, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x00}, {0x41, 0x04}, {0x41, 0x04}, {0x41, 0x07}, {0x41, 0x07}, {0xFF, 0x01}},
     {{0x86, 0x00}, {0xFF, 0x00}}, {}},
    // --- drums (absolute pitches: any note plays the same drum) ----------------
    {"KICK", kDrum, "pulse thump falling to triangle", 0x00, 0xE9, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xC4}, {0x41, 0xA8}, {0x41, 0xA2}, {0x11, 0x9E}, {0x11, 0x9A}, {0x10, 0x96}, {0xFF, 0x00}},
     {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    {"DEEP KICK", kDrum, "longer, lower kick", 0x00, 0xF9, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xC0}, {0x11, 0xA4}, {0x11, 0x9F}, {0x11, 0x9B}, {0x11, 0x98}, {0x11, 0x95}, {0x10, 0x93}, {0xFF, 0x00}}, {}, {}},
    {"SNARE", kDrum, "noise and pulse body", 0x00, 0xF9, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xD8}, {0x41, 0xAA}, {0x81, 0xD4}, {0x80, 0xD1}, {0x80, 0xCE}, {0x80, 0xCC}, {0xFF, 0x00}},
     {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    {"CLAP", kDrum, "retriggered noise bursts", 0x03, 0x00, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xD0}, {0x80, 0xD0}, {0x81, 0xD0}, {0x80, 0xD0}, {0x81, 0xD0}, {0x80, 0xD0}, {0xFF, 0x00}}, {}, {}},
    {"HIHAT", kDrum, "one tick of high noise", 0x00, 0x00, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xDF}, {0x80, 0xDF}, {0xFF, 0x00}}, {}, {}},
    {"OPEN HAT", kDrum, "high noise, longer decay", 0x07, 0x00, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xDE}, {0xFF, 0x00}}, {}, {}},
    {"TOM", kDrum, "pulse tom falling in pitch", 0x00, 0xC9, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xB8}, {0x41, 0xB0}, {0x41, 0xAD}, {0x41, 0xAA}, {0x41, 0xA8}, {0x41, 0xA6}, {0x40, 0xA4}, {0xFF, 0x00}},
     {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    {"RIMSHOT", kDrum, "short noise and pulse click", 0x02, 0x00, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xDC}, {0x41, 0xC4}, {0x40, 0xC4}, {0xFF, 0x00}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"ZAP DRUM", kDrum, "fast falling pulse", 0x00, 0xA9, 0x09, 0x00, 0, 0, 0,
     {{0x41, 0xD8}, {0x41, 0xCC}, {0x41, 0xC0}, {0x41, 0xB4}, {0x41, 0xA8}, {0x41, 0x9C}, {0x40, 0x90}, {0xFF, 0x00}},
     {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    // --- pads -----------------------------------------------------------------
    {"SAW PAD", kPad, "slow saw, band pass sweep", 0x8A, 0xCB, 0x09, 0x42, 0, 0, 0,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {{0xA0, 0xC0}, {0x00, 0x10}, {0x60, 0x01}, {0x60, 0xFF}, {0xFF, 0x03}}},
    {"TRI PAD", kPad, "slow triangle, gentle vibrato", 0x8A, 0xCB, 0x09, 0x42, 0x03, 0x08, 32,
     {{0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"PWM PAD", kPad, "slowly breathing pulse", 0xA8, 0xCA, 0x09, 0x42, 0, 0, 0,
     {{0x41, 0x00}, {0xFF, 0x00}}, {{0x84, 0x00}, {0x7F, 0x03}, {0x7F, 0xFD}, {0xFF, 0x02}}, {}},
    {"SWEEP PAD", kPad, "low pass opening and closing", 0xC9, 0xAC, 0x09, 0x42, 0x03, 0x30, 48,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {{0x90, 0xE0}, {0x00, 0x08}, {0x7F, 0x01}, {0x7F, 0xFF}, {0xFF, 0x03}}},
    {"STRINGS", kPad, "saw, soft low pass, vibrato", 0x99, 0xAB, 0x09, 0x42, 0x04, 0x10, 24,
     {{0x21, 0x00}, {0xFF, 0x00}}, {}, {{0x90, 0x80}, {0x00, 0x90}, {0xFF, 0x00}}},
    // --- effects --------------------------------------------------------------
    {"LASER", kFx, "pitch falling fast", 0x00, 0xA0, 0x09, 0x00, 0, 0, 0,
     {{0x41, 0xDA}, {0x41, 0xD4}, {0x41, 0xCE}, {0x41, 0xC8}, {0x41, 0xC2}, {0x41, 0xBC}, {0x41, 0xB6}, {0x41, 0xB0},
      {0x40, 0xB0}, {0xFF, 0x00}}, {{0x82, 0x00}, {0xFF, 0x00}}, {}},
    {"EXPLOSION", kFx, "falling noise, long decay", 0x0C, 0x00, 0x09, 0x00, 0, 0, 0,
     {{0x81, 0xB4}, {0x81, 0xAE}, {0x81, 0xA8}, {0x81, 0xA4}, {0x81, 0xA0}, {0x81, 0x9C}, {0xFF, 0x00}}, {}, {}},
    {"SIREN", kFx, "triangle between two notes", 0x0A, 0xF0, 0x09, 0x02, 0, 0, 0,
     {{0x11, 0x00}, {0x11, 0x00}, {0x11, 0x00}, {0x11, 0x00}, {0x11, 0x05}, {0x11, 0x05}, {0x11, 0x05}, {0x11, 0x05},
      {0xFF, 0x01}}, {}, {}},
    {"COIN", kFx, "two quick steps up", 0x00, 0x88, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x0C}, {0x41, 0x13}, {0x41, 0x18}, {0x41, 0x18}, {0x40, 0x18}, {0xFF, 0x00}}, {{0x84, 0x00}, {0xFF, 0x00}}, {}},
    {"WIND", kFx, "noise through a moving band pass", 0x8A, 0xCA, 0x09, 0x42, 0, 0, 0,
     {{0x81, 0xA0}, {0xFF, 0x00}}, {}, {{0xA0, 0xF0}, {0x00, 0x20}, {0x40, 0x01}, {0x40, 0xFF}, {0xFF, 0x03}}},
    {"POWER UP", kFx, "major scale up an octave", 0x00, 0xF0, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x02}, {0x41, 0x04}, {0x41, 0x05}, {0x41, 0x07}, {0x41, 0x09}, {0x41, 0x0B}, {0x41, 0x0C},
      {0xFF, 0x00}}, {{0x86, 0x00}, {0xFF, 0x00}}, {}},
    // --- keys / bells ---------------------------------------------------------
    {"RING BELL", kKeys, "ring-modulated triangle (needs the voice before it playing)", 0x09, 0x0A, 0x15, 0x02, 0, 0, 0,
     {{0x15, 0x00}, {0x11, 0x0C}, {0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"E-PIANO", kKeys, "octave attack, closing pulse", 0x09, 0x49, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x0C}, {0x41, 0x00}, {0xFF, 0x00}}, {{0x86, 0x00}, {0x40, 0xF8}, {0xFF, 0x00}}, {}},
    {"CLAV", kKeys, "very narrow pulse, short", 0x05, 0x04, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0xFF, 0x00}}, {{0x81, 0x80}, {0xFF, 0x00}}, {}},
    {"ORGAN", kKeys, "root and octave every tick", 0x00, 0xF2, 0x09, 0x02, 0, 0, 0,
     {{0x41, 0x00}, {0x41, 0x0C}, {0xFF, 0x01}}, {{0x88, 0x00}, {0xFF, 0x00}}, {}},
    {"MARIMBA", kKeys, "short triangle with an octave click", 0x07, 0x06, 0x09, 0x02, 0, 0, 0,
     {{0x11, 0x0C}, {0x11, 0x00}, {0xFF, 0x00}}, {}, {}},
    {"CHIME", kKeys, "ring-modulated octave", 0x0A, 0x0A, 0x15, 0x02, 0, 0, 0,
     {{0x15, 0x0C}, {0x15, 0x00}, {0xFF, 0x00}}, {}, {}},
};
// clang-format on
constexpr int kCount = int(sizeof kPresets / sizeof kPresets[0]);

const char* const kCategoryNames[kCategories] = {"BASS", "LEAD", "CHORD", "DRUM", "PAD", "FX", "KEYS"};
const char* const kChordNames[kChords] = {"MAJOR", "MINOR", "7TH", "MAJ7", "MIN7", "SUS2", "SUS4",
                                          "DIM",   "AUG",   "POWER", "OCTAVE", "6TH", "MIN9"};
const int8_t kChordNotes[kChords][4] = {{0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10},
                                        {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 7, 12, -1},
                                        {0, 12, -1, -1}, {0, 4, 7, 9}, {0, 3, 7, 14}};
const char* const kOrderNames[kOrders] = {"UP", "DOWN", "UP-DOWN", "UP + OCTAVE"};
const char* const kWaveNames[kArpWaves] = {"PULSE", "SAW", "TRIANGLE", "SAW+TRI"};
const uint8_t kWaveBytes[kArpWaves] = {0x41, 0x21, 0x11, 0x31};

// Rows up to and including the first FF (a program ends with a jump / stop).
int programLength(const Row* rows, int cap) {
    if (!rows[0].l && !rows[0].r) return 0;
    for (int i = 0; i < cap; ++i)
        if (rows[i].l == 0xFF) return i + 1;
    return cap;
}

Row relocate(Row r, int start) {
    if (r.l == 0xFF && r.r) r.r = uint8_t(start + r.r - 1);
    return r;
}

// Where `rows` go in table `t`: an identical program already there, else
// after the last used row. 0 = no room.
int place(const Table& t, const Row* rows, int n) {
    const int used = t.used();
    for (int s = 1; s + n - 1 <= used; ++s) {
        bool same = true;
        for (int i = 0; i < n && same; ++i) {
            const Row r = relocate(rows[i], s);
            same = t.l(s + i) == r.l && t.r(s + i) == r.r;
        }
        if (same) return s;
    }
    const int start = used + 1;
    return start + n - 1 <= kTableLen ? start : 0;
}

void write(Table& t, const Row* rows, int n, int start) {
    for (int i = 0; i < n; ++i) {
        const Row r = relocate(rows[i], start);
        t.left[start + i - 1] = r.l;
        t.right[start + i - 1] = r.r;
    }
}

int speedRow(const Table& t, uint8_t l, uint8_t r) {
    const Row row{l, r};
    return place(t, &row, 1);
}

}  // namespace

const char* categoryName(int c) { return c >= 0 && c < kCategories ? kCategoryNames[c] : "?"; }
int presetCount() { return kCount; }
const Preset& preset(int i) { return kPresets[i < 0 ? 0 : (i >= kCount ? kCount - 1 : i)]; }

int categoryFirst(int c) {
    for (int i = 0; i < kCount; ++i)
        if (kPresets[i].category == c) return i;
    return 0;
}

int categoryCount(int c) {
    int n = 0;
    for (const Preset& p : kPresets) n += p.category == c;
    return n;
}

const char* resultText(Result r) { return r == Result::kOk ? "ok" : "table full"; }

Result load(Song& s, int slot, const Preset& p, int voice) {
    if (slot < 1 || slot >= kInstrumentCount) return Result::kTableFull;
    Row filt[8];
    std::memcpy(filt, p.filter, sizeof filt);
    if (filt[0].l >= 0x80 && filt[0].l != 0xFF) filt[0].r = uint8_t((filt[0].r & 0xF0) | (1 << (voice % 3)));
    const int nw = programLength(p.wave, 16), np = programLength(p.pulse, 8), nf = programLength(filt, 8);
    const bool vib = p.vibDelay && (p.vibSpeed || p.vibDepth);
    // Check every table first: a full table leaves the song unchanged.
    const int sw = nw ? place(s.tables[kWaveTable], p.wave, nw) : -1;
    const int sp = np ? place(s.tables[kPulseTable], p.pulse, np) : -1;
    const int sf = nf ? place(s.tables[kFilterTable], filt, nf) : -1;
    const int sv = vib ? speedRow(s.tables[kSpeedTable], p.vibSpeed, p.vibDepth) : -1;
    if (!sw || !sp || !sf || !sv) return Result::kTableFull;
    if (nw) write(s.tables[kWaveTable], p.wave, nw, sw);
    if (np) write(s.tables[kPulseTable], p.pulse, np, sp);
    if (nf) write(s.tables[kFilterTable], filt, nf, sf);
    if (vib) {
        const Row row{p.vibSpeed, p.vibDepth};
        write(s.tables[kSpeedTable], &row, 1, sv);
    }
    Instrument& in = s.inst[slot];
    in.clear(slot);
    std::snprintf(in.name, sizeof in.name, "%s", p.name);
    in.ad = p.ad;
    in.sr = p.sr;
    in.firstWave = p.firstWave;
    in.gateTimer = p.gateTimer;
    in.wavePtr = uint8_t(nw ? sw : 0);
    in.pulsePtr = uint8_t(np ? sp : 0);
    in.filterPtr = uint8_t(nf ? sf : 0);
    in.vibParam = uint8_t(vib ? sv : 0);
    in.vibDelay = vib ? p.vibDelay : 0;
    return Result::kOk;
}

const char* chordName(int c) { return c >= 0 && c < kChords ? kChordNames[c] : "?"; }
const char* orderName(int o) { return o >= 0 && o < kOrders ? kOrderNames[o] : "?"; }
const char* arpWaveName(int w) { return w >= 0 && w < kArpWaves ? kWaveNames[w] : "?"; }

int arpNotes(const Arp& a, int8_t* out, int cap) {
    int8_t base[5];
    int n = 0;
    for (int i = 0; i < 4 && kChordNotes[a.chord % kChords][i] >= 0; ++i) base[n++] = kChordNotes[a.chord % kChords][i];
    int8_t seq[10];
    int m = 0;
    switch (a.order % kOrders) {
        case kDown:
            for (int i = n - 1; i >= 0; --i) seq[m++] = base[i];
            break;
        case kUpDown:
            for (int i = 0; i < n; ++i) seq[m++] = base[i];
            for (int i = n - 2; i >= 1; --i) seq[m++] = base[i];
            break;
        case kUpOctave:
            for (int i = 0; i < n; ++i) seq[m++] = base[i];
            seq[m++] = int8_t(base[0] + 12);
            break;
        default:
            for (int i = 0; i < n; ++i) seq[m++] = base[i];
            break;
    }
    if (m > cap) m = cap;
    for (int i = 0; i < m; ++i) out[i] = seq[i];
    return m;
}

int arpProgram(const Arp& a, Row* out, int cap) {
    int8_t notes[10];
    const int n = arpNotes(a, notes, 10);
    const int ticks = a.ticks < 1 ? 1 : (a.ticks > 4 ? 4 : a.ticks);
    int k = 0;
    const uint8_t w = kWaveBytes[a.wave % kArpWaves];
    for (int i = 0; i < n; ++i)
        for (int t = 0; t < ticks && k < cap - 1; ++t) out[k++] = {w, uint8_t(notes[i])};
    out[k++] = {0xFF, 0x01};
    return k;
}

Result insertArp(Song& s, int slot, const Arp& a) {
    if (slot < 1 || slot >= kInstrumentCount) return Result::kTableFull;
    Row rows[41];
    const int k = arpProgram(a, rows, 41);
    Instrument& in = s.inst[slot];
    const Row pulse[2] = {{0x88, 0x00}, {0xFF, 0x00}};
    const bool needPulse = a.wave % kArpWaves == kPulse && !in.pulsePtr;
    const int sw = place(s.tables[kWaveTable], rows, k);
    const int sp = needPulse ? place(s.tables[kPulseTable], pulse, 2) : -1;
    if (!sw || !sp) return Result::kTableFull;
    write(s.tables[kWaveTable], rows, k, sw);
    in.wavePtr = uint8_t(sw);
    if (needPulse) {
        write(s.tables[kPulseTable], pulse, 2, sp);
        in.pulsePtr = uint8_t(sp);
    }
    return Result::kOk;
}

}  // namespace library
}  // namespace sidt
