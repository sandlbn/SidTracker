// text2sidt -- builds a SIDTracker project from a plain song text (written
// by importers such as tools/blastgrid/bg2text.py) with the app's own song
// model and project writer. Host tool.
//
//   text2sidt IN.txt OUT.sidt
// Lines (values hex where noted):
//   song NAME... / author NAME... / bpm N / speed N / hr AD SR (hex)
//   model 6581|8580 6581|8580 / balance N
//   inst SLOT AD SR FIRSTWAVE WAVEPTR PULSEPTR FILTERPTR VIBPARAM VIBDELAY GATE NAME...   (hex)
//   table wave|pulse|filter|speed ROW L R          (ROW decimal, L R hex)
//   pattern P ROWS
//   cell P ROW TRACK NOTE INST CMD PARAM           (P ROW TRACK decimal, rest hex)
//   order PATTERN TRANSPOSE REPEAT / restart POS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "tracker/song/project_file.h"

using namespace sidt;

namespace {
struct FileWriter : project::Writer {
    std::FILE* f;
    bool write(const void* d, uint32_t n) override { return std::fwrite(d, 1, n, f) == n; }
};
unsigned hx(const char* s) { return unsigned(std::strtoul(s, nullptr, 16)); }
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: text2sidt IN.txt OUT.sidt\n");
        return 2;
    }
    std::FILE* in = std::fopen(argv[1], "r");
    if (!in) return std::perror(argv[1]), 1;
    std::unique_ptr<Pattern[]> pats(new Pattern[kPatternCount]);
    std::unique_ptr<Song> sp(new Song);
    Song& s = *sp;
    s.patterns = pats.get();
    s.clear();
    for (Table& t : s.tables) t = Table{};
    for (int i = 1; i < kInstrumentCount; ++i) s.inst[i].clear(i);
    s.orderLen = 0;
    char line[512];
    int ln = 0;
    while (std::fgets(line, sizeof line, in)) {
        ++ln;
        line[std::strcspn(line, "\r\n")] = 0;
        char* tok[16] = {};
        int n = 0;
        char* rest = nullptr;   // text after the fixed fields (names)
        for (char* p = std::strtok(line, " "); p && n < 16; p = std::strtok(nullptr, n ? " " : " ")) {
            tok[n++] = p;
        }
        if (!n) continue;
        const std::string k = tok[0];
        auto textFrom = [&](int i, char* out, size_t cap) {
            std::string t;
            for (int j = i; j < n; ++j) t += (j > i ? " " : "") + std::string(tok[j]);
            std::snprintf(out, cap, "%s", t.c_str());
        };
        (void)rest;
        if (k == "song") textFrom(1, s.name, sizeof s.name);
        else if (k == "author") textFrom(1, s.author, sizeof s.author);
        else if (k == "bpm" && n > 1) s.bpm = uint8_t(std::atoi(tok[1]));
        else if (k == "speed" && n > 1) s.speed = uint8_t(std::atoi(tok[1]));
        else if (k == "hr" && n > 2) {
            s.hrAD = uint8_t(hx(tok[1]));
            s.hrSR = uint8_t(hx(tok[2]));
        } else if (k == "model" && n > 2) {
            for (int c = 0; c < 2; ++c) s.sid.model[c] = std::atoi(tok[1 + c]) == 6581 ? sid::Model::k6581 : sid::Model::k8580;
        } else if (k == "balance" && n > 1) {
            s.sid.balance = uint8_t(std::atoi(tok[1]));
        } else if (k == "inst" && n > 10) {
            const int slot = std::atoi(tok[1]);
            if (slot < 1 || slot >= kInstrumentCount) return std::fprintf(stderr, "%d: bad slot\n", ln), 1;
            Instrument& i = s.inst[slot];
            i.ad = uint8_t(hx(tok[2]));
            i.sr = uint8_t(hx(tok[3]));
            i.firstWave = uint8_t(hx(tok[4]));
            i.wavePtr = uint8_t(hx(tok[5]));
            i.pulsePtr = uint8_t(hx(tok[6]));
            i.filterPtr = uint8_t(hx(tok[7]));
            i.vibParam = uint8_t(hx(tok[8]));
            i.vibDelay = uint8_t(hx(tok[9]));
            i.gateTimer = uint8_t(hx(tok[10]));
            textFrom(11, i.name, sizeof i.name);
        } else if (k == "table" && n > 4) {
            const char* kinds[kTableKinds] = {"wave", "pulse", "filter", "speed"};
            int t = 0;
            while (t < kTableKinds && std::strcmp(tok[1], kinds[t])) ++t;
            const int row = std::atoi(tok[2]);
            if (t == kTableKinds || row < 1 || row > kTableLen) return std::fprintf(stderr, "%d: bad table row\n", ln), 1;
            s.tables[t].left[row - 1] = uint8_t(hx(tok[3]));
            s.tables[t].right[row - 1] = uint8_t(hx(tok[4]));
        } else if (k == "pattern" && n > 2) {
            const int p = std::atoi(tok[1]), rows = std::atoi(tok[2]);
            if (p < 0 || p >= kPatternCount || rows < 1 || rows > kMaxRows)
                return std::fprintf(stderr, "%d: bad pattern\n", ln), 1;
            s.pattern(p).clear(uint8_t(rows));
        } else if (k == "cell" && n > 7) {
            const int p = std::atoi(tok[1]), r = std::atoi(tok[2]), ch = std::atoi(tok[3]);
            if (p < 0 || p >= kPatternCount || r < 0 || r >= kMaxRows || ch < 0 || ch >= kChannels)
                return std::fprintf(stderr, "%d: bad cell\n", ln), 1;
            s.pattern(p).cells[r][ch] = {uint8_t(hx(tok[4])), uint8_t(hx(tok[5])), uint8_t(hx(tok[6])), uint8_t(hx(tok[7]))};
        } else if (k == "order" && n > 3) {
            if (s.orderLen >= 255) return std::fprintf(stderr, "%d: order too long\n", ln), 1;
            s.order[s.orderLen++] = {uint8_t(std::atoi(tok[1])), int8_t(std::atoi(tok[2])), uint8_t(std::atoi(tok[3]))};
        } else if (k == "restart" && n > 1) {
            s.restartPos = uint8_t(std::atoi(tok[1]));
        } else {
            std::fprintf(stderr, "%s:%d: unknown line '%s'\n", argv[1], ln, tok[0]);
            return 1;
        }
    }
    std::fclose(in);
    if (!s.orderLen) s.orderLen = 1;
    FileWriter w;
    w.f = std::fopen(argv[2], "wb");
    if (!w.f) return std::perror(argv[2]), 1;
    const bool ok = project::save(s, w) == project::Result::kOk;
    std::fclose(w.f);
    if (!ok) std::fprintf(stderr, "%s: write error\n", argv[2]);
    return ok ? 0 : 1;
}
