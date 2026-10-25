// make_instruments -- the stock instrument library for the card (MIT, made
// for the release zip): every built-in SOUNDS preset as a GoatTracker .ins
// file in a folder per category, plus extra instruments in "Classic" built
// on familiar C64 techniques. SIDTracker lists them in SOUNDS > CARD;
// GoatTracker loads them too.
//   make_instruments OUTDIR
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <sys/stat.h>

#include "compatibility/goat/export.h"
#include "compatibility/goat/gt_format.h"
#include "tracker/song/sound_library.h"
#include "classic_instruments.h"

using namespace sidt;
using library::Preset;

namespace {

struct FileSink : goat::Sink {
    std::FILE* f;
    bool put(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};

std::string folderOf(int c) {
    std::string n = library::categoryName(c);   // "BASS" -> "Bass"
    for (size_t i = 1; i < n.size(); ++i) n[i] = char(std::tolower(static_cast<unsigned char>(n[i])));
    return n;
}

bool writeOne(const Preset& p, const std::string& dir) {
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> s(new Song);
    s->patterns = pats.get();
    s->clear();
    for (Table& t : s->tables) t = Table{};   // the preset's rows only
    s->inst[1].clear(1);
    if (library::load(*s, 1, p, 0) != library::Result::kOk) return false;
    goat::InsFile ins;
    if (!goat::exportInstrument(*s, 1, ins)) return false;
    std::string file = p.name;
    for (char& c : file)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') c = '_';
    mkdir(dir.c_str(), 0755);
    const std::string path = dir + "/" + file + ".ins";
    FileSink out;
    out.f = std::fopen(path.c_str(), "wb");
    if (!out.f) return std::perror(path.c_str()), false;
    const bool ok = goat::writeIns(ins, out);
    std::fclose(out.f);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_instruments OUTDIR\n");
        return 2;
    }
    const std::string out = argv[1];
    mkdir(out.c_str(), 0755);
    int n = 0;
    for (int i = 0; i < library::presetCount(); ++i) {
        const Preset& p = library::preset(i);
        if (!writeOne(p, out + "/" + folderOf(p.category))) return std::fprintf(stderr, "%s failed\n", p.name), 1;
        ++n;
    }
    for (const Preset& p : classic::kClassic) {
        if (!writeOne(p, out + "/Classic")) return std::fprintf(stderr, "%s failed\n", p.name), 1;
        ++n;
    }
    std::printf("%s: %d instruments\n", out.c_str(), n);
    return 0;
}
