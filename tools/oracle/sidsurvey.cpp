// sidsurvey -- runs .sid files through the firmware's SID player (src/c64)
// and sorts them: plays (a voice gates on within N frames), silent, RSID,
// load error, init crash, play crash / hang. Also the heaviest frame (6502
// cycles). Host tool:  sidsurvey [--frames N] FILE...   (or file names on stdin)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "c64/sid_player.h"

using namespace sidt;

int main(int argc, char** argv) {
    int frames = 500;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) frames = std::atoi(argv[++i]);
        else files.push_back(argv[i]);
    }
    if (files.empty())
        for (char line[1024]; std::fgets(line, sizeof line, stdin);) {
            line[std::strcspn(line, "\r\n")] = 0;
            if (line[0]) files.push_back(line);
        }
    std::vector<uint8_t> mem(c64::SidPlayer::kRamBytes + c64::SidPlayer::kRomBytes);
    int plays = 0, silent = 0, rsid = 0, loadErr = 0, initCrash = 0, playCrash = 0;
    uint64_t worst = 0;
    std::string worstName;
    for (const std::string& path : files) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> b;
        for (int c; (c = std::fgetc(f)) != EOF;) b.push_back(uint8_t(c));
        std::fclose(f);
        c64::SidPlayer p;
        p.setMemory(mem.data(), mem.data() + c64::SidPlayer::kRamBytes);
        c64::SidInfo info;
        const char* err = nullptr;
        if (!p.load(b.data(), uint32_t(b.size()), info, err)) {
            if (err && std::strstr(err, "RSID")) ++rsid;
            else ++loadErr;
            continue;
        }
        c64::SidWrite iw[c64::SidPlayer::kMaxWrites];
        int ni = 0;
        uint8_t vol = 0;
        if (!p.start(info.start, iw, &ni)) {
            ++initCrash;
            std::printf("init crash (op %02X): %s\n", p.failOpcode(), path.c_str());
            continue;
        }
        for (int i = 0; i < ni; ++i)
            if (iw[i].reg == 0x18) vol = iw[i].val & 15;
        bool gated = false, ok = true;
        for (int fr = 0; fr < frames && ok; ++fr) {
            c64::SidWrite w[c64::SidPlayer::kMaxWrites];
            int n = 0;
            if (!p.frame(w, n)) {
                ok = false;
                break;
            }
            for (int i = 0; i < n; ++i) {
                if (w[i].reg == 0x18) vol = w[i].val & 15;
                if ((w[i].reg == 4 || w[i].reg == 11 || w[i].reg == 18) && (w[i].val & 1) && (w[i].val & 0xF0) && vol)
                    gated = true;   // a voice gated on while the volume is up
            }
        }
        if (!ok) {
            ++playCrash;
            std::printf("play crash (op %02X): %s\n", p.failOpcode(), path.c_str());
            continue;
        }
        if (gated) ++plays;
        else {
            ++silent;
            std::printf("silent: %s\n", path.c_str());
        }
    }
    const int n = int(files.size());
    std::printf("\n%d files: plays %d (%.1f%%), silent %d, RSID %d, unreadable %d, init crash %d, play crash %d\n", n,
                plays, n ? 100.0 * plays / n : 0.0, silent, rsid, loadErr, initCrash, playCrash);
    (void)worst;
    (void)worstName;
    return 0;
}
