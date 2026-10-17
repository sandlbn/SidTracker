// gtcompare -- plays a GoatTracker song through SIDTracker's importer and
// player and compares the SID register state after every tick with a
// sidtrace log of the same song packed by GoatTracker's gt2reloc
// (tools/oracle/run.sh, docs/goattracker-format.md "Oracle").
//
//   gtcompare SONG.sng TRACE.txt [--frames N] [--subtune N] [--show N]
// Exit 0 when every compared frame matches.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "compatibility/goat/import.h"
#include "tracker/playback/player.h"

using namespace sidt;

namespace {

struct Sink : ISidSink {
    uint8_t regs[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { regs[chip & 1][reg & 31] = val; }
};

constexpr int kRegs = 0x19;
const char* const kRegNames[kRegs] = {"f1lo", "f1hi", "p1lo", "p1hi", "ctl1", "ad1", "sr1", "f2lo", "f2hi",
                                      "p2lo", "p2hi", "ctl2", "ad2", "sr2", "f3lo", "f3hi", "p3lo", "p3hi",
                                      "ctl3", "ad3", "sr3", "fclo", "fchi", "res", "modevol"};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gtcompare SONG.sng TRACE.txt [--frames N] [--subtune N] [--show N]\n");
        return 2;
    }
    int frames = 3000, subtune = 0, show = 3;
    const char* focus = nullptr;
    int traceFrom = -1, traceTo = -1;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--subtune") && i + 1 < argc) subtune = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--show") && i + 1 < argc) show = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--reg") && i + 1 < argc) focus = argv[++i];
        else if (!std::strcmp(argv[i], "--trace") && i + 2 < argc) {
            traceFrom = std::atoi(argv[++i]);
            traceTo = std::atoi(argv[++i]);
        }
    }
    std::FILE* f = std::fopen(argv[1], "rb");
    if (!f) return std::perror(argv[1]), 1;
    std::vector<uint8_t> bytes;
    for (int c; (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
    std::fclose(f);
    std::unique_ptr<goat::Song> gt(new goat::Song);
    const goat::Result r = goat::readSong({bytes.data(), uint32_t(bytes.size())}, *gt);
    if (r != goat::Result::kOk) {
        std::fprintf(stderr, "%s: %s\n", argv[1], goat::resultText(r));
        return 1;
    }
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    goat::Report rep;
    if (!goat::importSong(*gt, subtune, *song, rep)) {
        for (int i = 0; i < rep.lines; ++i) std::fprintf(stderr, "import: %s\n", rep.line[i]);
        return 1;
    }

    // Oracle trace.
    std::vector<std::vector<uint8_t>> ref;
    std::FILE* t = std::fopen(argv[2], "r");
    if (!t) return std::perror(argv[2]), 1;
    char line[512];
    while (std::fgets(line, sizeof line, t)) {
        if (line[0] == '#') continue;
        char* p = std::strchr(line, ':');
        if (!p) continue;
        std::vector<uint8_t> v;
        for (int i = 0; i < kRegs; ++i) v.push_back(uint8_t(std::strtoul(p + 1, &p, 16)));
        ref.push_back(v);
    }
    std::fclose(t);

    // Bits the SID ignores (PW hi bits 4-7, FC lo bits 3-7) are not compared:
    // GoatTracker writes the pulse table byte to PW hi unmasked.
    auto mask = [](std::vector<uint8_t>& v) {
        v[0x03] &= 0x0F;
        v[0x0A] &= 0x0F;
        v[0x11] &= 0x0F;
        v[0x15] &= 0x07;
    };
    for (auto& v : ref) mask(v);
    // Inaudible: a voice with no waveform bits in GT. Its frequency and pulse
    // (lead-in effects on unused channels) are not compared.
    auto silentVoice = [](const std::vector<uint8_t>& g, int v) { return (g[v * 7 + 4] & 0xF0) == 0; };
    // Ours, one tick per frame.
    Sink sink;
    Player pl;
    pl.init(song.get(), &sink);
    pl.playSong(0);
    std::vector<std::vector<uint8_t>> ours;
    std::vector<std::string> pos;
    const int n = frames < int(ref.size()) ? frames : int(ref.size());
    for (int i = 0; i < n + 64; ++i) {
        char p[96];
        const Cell* cr = song->pattern(pl.patternIndex()).cells[pl.row()];
        std::snprintf(p, sizeof p, "pat %02X row %02X t%u s%u |%02X %02X %X%02X|%02X %02X %X%02X|%02X %02X %X%02X",
                      unsigned(pl.patternIndex()), unsigned(pl.row()), unsigned(pl.tickInRow()), unsigned(pl.speed()),
                      cr[0].note, cr[0].inst, cr[0].cmd, cr[0].param, cr[1].note, cr[1].inst, cr[1].cmd, cr[1].param,
                      cr[2].note, cr[2].inst, cr[2].cmd, cr[2].param);
        pos.emplace_back(p);
        pl.tick();
        ours.emplace_back(sink.regs[0], sink.regs[0] + kRegs);
        mask(ours.back());
    }
    // Alignment: GoatTracker starts with an empty lead-in row; find the frame
    // offset with the most matching registers over the first 600 frames.
    int best = 0, bestScore = -1;
    for (int off = 0; off < 48; ++off) {
        int score = 0;
        for (int i = 0; i < 600 && i + off < n; ++i)
            for (int k = 0; k < kRegs; ++k) score += ours[i][k] == ref[i + off][k];
        if (score > bestScore) bestScore = score, best = off;
    }
    int bad = 0, shown = 0, compared = 0;
    int perReg[kRegs] = {};
    for (int i = 0; i + best < n; ++i) {
        ++compared;
        bool frameBad = false;
        for (int k = 0; k < kRegs; ++k) {
            if (k < 21 && k % 7 < 4 && silentVoice(ref[i + best], k / 7)) continue;
            if (ours[i][k] != ref[i + best][k]) {
                ++perReg[k];
                frameBad = true;
            }
        }
        if (!frameBad) continue;
        ++bad;
        if (shown++ < show) {
            std::printf("  tick %d (GT frame %d):\n    GT  ", i, i + best);
            for (int k = 0; k < kRegs; ++k) std::printf(" %02X", ref[i + best][k]);
            std::printf("\n    us  ");
            for (int k = 0; k < kRegs; ++k)
                std::printf(ours[i][k] != ref[i + best][k] ? " %02X" : "   ", ours[i][k]);
            std::printf("\n");
        }
    }
    if (focus) {
        // The voice's registers around the first mismatches of one register.
        int k = -1;
        for (int i = 0; i < kRegs; ++i)
            if (!std::strcmp(kRegNames[i], focus)) k = i;
        const int v0 = k >= 0 && k < 21 ? k / 7 * 7 : 21;
        int printed = 0;
        for (int i = 0; k >= 0 && i + best < n && printed < show; ++i) {
            if (ours[i][k] == ref[i + best][k]) continue;
            ++printed;
            std::printf("  %s mismatch at tick %d (row tick %d):\n", focus, i, -1);
            for (int j = i - 4 < 0 ? 0 : i - 4; j <= i + 4 && j + best < n; ++j) {
                std::printf("   %5d GT", j);
                for (int r = v0; r < v0 + (v0 < 21 ? 7 : 4); ++r) std::printf(" %02X", ref[j + best][r]);
                std::printf("  us");
                for (int r = v0; r < v0 + (v0 < 21 ? 7 : 4); ++r) std::printf(" %02X", ours[j][r]);
                std::printf("%s\n", j == i ? "  <" : "");
            }
            while (i + best < n && ours[i][k] != ref[i + best][k]) ++i;
        }
    }
    for (int i = traceFrom; i >= 0 && i <= traceTo && i + best < n; ++i) {
        std::printf("  %5d %-62s GT", i, pos[i].c_str());
        for (int k = 0; k < kRegs; ++k) std::printf(" %02X", ref[i + best][k]);
        std::printf("\n  %5d %-62s us", i, "");
        for (int k = 0; k < kRegs; ++k) std::printf(ours[i][k] != ref[i + best][k] ? " %02X" : "   ", ours[i][k]);
        std::printf("\n");
    }
    std::printf("%s: offset %d, %d/%d frames differ", argv[1], best, bad, compared);
    for (int k = 0; k < kRegs; ++k)
        if (perReg[k]) std::printf(" %s:%d", kRegNames[k], perReg[k]);
    std::printf("\n");
    return bad ? 3 : 0;
}
