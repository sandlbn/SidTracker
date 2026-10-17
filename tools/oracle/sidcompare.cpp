// sidcompare -- M9 check of the .sid export: exports a song with the app's
// exporter (src/compatibility/sid), runs the file's init and play routines
// on the 6502 emulator and compares both SIDs' registers after every frame
// with SIDTracker's own player (docs/sid-export.md).
//
//   sidcompare SONG.sidt|SONG.sng [--frames N] [--subtune N] [--out FILE.sid] [--show N]
// Exit 0 when every frame matches.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "compatibility/goat/import.h"
#include "compatibility/sid/sid_export.h"
#include "cpu6502.h"
#include "tracker/playback/player.h"
#include "tracker/song/project_file.h"

using namespace sidt;

namespace {

struct Sink : ISidSink {
    uint8_t regs[2][32] = {};
    void sidWrite(uint8_t chip, uint8_t reg, uint8_t val) override { regs[chip & 1][reg & 31] = val; }
};

struct MemReader : project::Reader {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    explicit MemReader(const std::vector<uint8_t>& v) : b(v) {}
    int32_t read(void* d, uint32_t n) override {
        if (pos + n > b.size()) n = uint32_t(b.size() - pos);
        std::memcpy(d, b.data() + pos, n);
        pos += n;
        return int32_t(n);
    }
};

const char* const kRegNames[0x19] = {"f1lo", "f1hi", "p1lo", "p1hi", "ctl1", "ad1",  "sr1",  "f2lo", "f2hi",
                                     "p2lo", "p2hi", "ctl2", "ad2",  "sr2",  "f3lo", "f3hi", "p3lo", "p3hi",
                                     "ctl3", "ad3",  "sr3",  "fclo", "fchi", "res",  "modevol"};

bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    for (int c; (c = std::fgetc(f)) != EOF;) out.push_back(uint8_t(c));
    std::fclose(f);
    return true;
}

bool endsWith(const std::string& s, const char* t) {
    const size_t n = std::strlen(t);
    return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: sidcompare SONG.sidt|SONG.sng [--frames N] [--subtune N] [--out FILE.sid] [--show N]\n");
        return 2;
    }
    int frames = 3000, subtune = 0, show = 3;
    const char* outPath = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--subtune") && i + 1 < argc) subtune = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!std::strcmp(argv[i], "--show") && i + 1 < argc) show = std::atoi(argv[++i]);
    }
    std::vector<uint8_t> bytes;
    if (!readFile(argv[1], bytes)) return std::perror(argv[1]), 1;
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    song->clear();
    if (endsWith(argv[1], ".sng")) {
        std::unique_ptr<goat::Song> gt(new goat::Song);
        if (goat::readSong({bytes.data(), uint32_t(bytes.size())}, *gt) != goat::Result::kOk) {
            std::fprintf(stderr, "%s: not a GoatTracker song\n", argv[1]);
            return 1;
        }
        goat::Report rep;
        if (!goat::importSong(*gt, subtune, *song, rep)) {
            std::fprintf(stderr, "%s: import failed\n", argv[1]);
            return 1;
        }
    } else {
        MemReader r(bytes);
        if (project::load(*song, r) != project::Result::kOk) {
            std::fprintf(stderr, "%s: not a SIDTracker project\n", argv[1]);
            return 1;
        }
    }

    // What a C64 cannot play is left out of the export (LFOs, automation);
    // compare with the player without it.
    if (song->mod.any() || song->automatedLanes()) std::printf("(LFOs / automation not exported: compared without)\n");
    song->mod = ModSettings{};
    for (int p = 0; p < kPatternCount; ++p) song->pattern(p).clearAuto(0xFFFF);

    std::vector<uint8_t> file(psid::kWorkBytes);
    const psid::ExportCheck check = psid::checkExport(*song, file.data(), uint32_t(file.size()));
    goat::Report rep;
    const uint32_t size = psid::exportSid(*song, file.data(), uint32_t(file.size()), rep);
    if (!size) {
        for (int i = 0; i < rep.lines; ++i) std::fprintf(stderr, "export: %s\n", rep.line[i]);
        return 1;
    }
    file.resize(size);
    if (outPath) {
        std::FILE* o = std::fopen(outPath, "wb");
        if (!o) return std::perror(outPath), 1;
        std::fwrite(file.data(), 1, file.size(), o);
        std::fclose(o);
    }

    // Load the PSID like a player would.
    oracle::Cpu cpu;
    const uint16_t init = uint16_t((file[10] << 8) | file[11]), play = uint16_t((file[12] << 8) | file[13]);
    const uint16_t load = uint16_t(file[0x7C] | (file[0x7D] << 8));
    for (size_t i = 0x7E; i < file.size(); ++i) cpu.mem[uint16_t(load + (i - 0x7E))] = file[i];
    if (!cpu.call(init, 0)) return 1;

    Sink sink;
    Player player;
    player.init(song.get(), &sink);
    player.playSong(0);
    const int chips = check.dual ? 2 : 1;
    int bad = 0;
    uint64_t maxCycles = 0, sumCycles = 0;
    std::vector<int> regBad(64, 0);
    for (int fr = 0; fr < frames; ++fr) {
        const uint64_t c0 = cpu.cycles;
        if (!cpu.call(play, 0)) {
            std::fprintf(stderr, "frame %d: play routine failed\n", fr);
            return 1;
        }
        const uint64_t dc = cpu.cycles - c0;
        sumCycles += dc;
        if (dc > maxCycles) maxCycles = dc;
        player.tick();
        bool diff = false;
        for (int c = 0; c < chips; ++c)
            for (int r = 0; r < 0x19; ++r)
                if (cpu.sid[c * 32 + r] != sink.regs[c][r]) {
                    diff = true;
                    ++regBad[c * 32 + r];
                }
        if (!diff) continue;
        if (bad < show) {
            std::printf("frame %d differs (row %u tick %u):\n", fr, unsigned(player.row()), unsigned(player.tickInRow()));
            for (int c = 0; c < chips; ++c)
                for (int r = 0; r < 0x19; ++r)
                    if (cpu.sid[c * 32 + r] != sink.regs[c][r])
                        std::printf("  SID%d %-7s c64 %02X player %02X\n", c + 1, kRegNames[r], cpu.sid[c * 32 + r],
                                    sink.regs[c][r]);
        }
        ++bad;
    }
    std::printf("%s: %u bytes, %s, %d/%d frames differ, play %llu avg / %llu max cycles", argv[1], unsigned(size),
                chips == 2 ? "2 SIDs" : "1 SID", bad, frames, (unsigned long long)(sumCycles / unsigned(frames)),
                (unsigned long long)maxCycles);
    for (int c = 0; c < chips; ++c)
        for (int r = 0; r < 0x19; ++r)
            if (regBad[c * 32 + r]) std::printf(" %s%s:%d", c ? "2." : "", kRegNames[r], regBad[c * 32 + r]);
    std::printf("\n");
    return bad ? 1 : 0;
}
