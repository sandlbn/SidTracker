// sidt2sng -- converts a SIDTracker project to a GoatTracker song with the
// same exporter the app uses; prints the export check. Host tool.
//   sidt2sng IN.sidt OUT.sng
#include <cstdio>
#include <memory>
#include <vector>

#include "compatibility/goat/export.h"
#include "tracker/song/project_file.h"

using namespace sidt;

namespace {
struct FileReader : project::Reader {
    std::FILE* f;
    int32_t read(void* d, uint32_t n) override { return int32_t(std::fread(d, 1, n, f)); }
};
struct FileSink : goat::Sink {
    std::FILE* f;
    bool put(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: sidt2sng IN.sidt OUT.sng\n");
        return 2;
    }
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> song(new Song);
    song->patterns = pats.get();
    FileReader r;
    r.f = std::fopen(argv[1], "rb");
    if (!r.f) return std::perror(argv[1]), 1;
    const project::Result res = project::load(*song, r);
    std::fclose(r.f);
    if (res != project::Result::kOk) {
        std::fprintf(stderr, "%s: %s\n", argv[1], project::resultText(res));
        return 1;
    }
    const goat::ExportCheck chk = goat::checkExport(*song);
    for (int i = 0; i < chk.report.lines; ++i) std::printf("  %s\n", chk.report.line[i]);
    std::unique_ptr<goat::Song> gt(new goat::Song);
    goat::Report rep;
    if (!goat::exportSong(*song, *gt, rep)) {
        for (int i = 0; i < rep.lines; ++i) std::fprintf(stderr, "export: %s\n", rep.line[i]);
        return 1;
    }
    FileSink s;
    s.f = std::fopen(argv[2], "wb");
    if (!s.f) return std::perror(argv[2]), 1;
    const bool ok = goat::writeSong(*gt, s);
    std::fclose(s.f);
    return ok ? 0 : 1;
}
