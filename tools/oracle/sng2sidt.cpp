// sng2sidt -- converts a GoatTracker song to a SIDTracker project with the
// importer the app uses, and prints the import report. Host tool.
//   sng2sidt IN.sng OUT.sidt [SUBTUNE]
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "compatibility/goat/import.h"
#include "tracker/song/project_file.h"

using namespace sidt;

namespace {
struct FileWriter : project::Writer {
    std::FILE* f;
    bool write(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: sng2sidt IN.sng OUT.sidt [SUBTUNE]\n");
        return 2;
    }
    std::FILE* in = std::fopen(argv[1], "rb");
    if (!in) return std::perror(argv[1]), 1;
    std::vector<uint8_t> bytes;
    for (int c; (c = std::fgetc(in)) != EOF;) bytes.push_back(uint8_t(c));
    std::fclose(in);
    std::unique_ptr<goat::Song> gt(new goat::Song);
    const goat::Result r = goat::readSong({bytes.data(), uint32_t(bytes.size())}, *gt);
    if (r != goat::Result::kOk) {
        std::fprintf(stderr, "%s: %s\n", argv[1], goat::resultText(r));
        return 1;
    }
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    song->clear();
    goat::Report rep;
    const bool ok = goat::importSong(*gt, argc > 3 ? std::atoi(argv[3]) : 0, *song, rep);
    for (int i = 0; i < rep.lines; ++i) std::printf("  %s\n", rep.line[i]);
    if (!ok) return 1;
    FileWriter w;
    w.f = std::fopen(argv[2], "wb");
    if (!w.f) return std::perror(argv[2]), 1;
    const bool saved = project::save(*song, w) == project::Result::kOk;
    std::fclose(w.f);
    return saved ? 0 : 1;
}
