// SIDTracker -- SID PLAYER screen (sid_player_screen.h).
#include "ui/sidplayer/sid_player_screen.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"
#include "ui/ui.h"

namespace sidt {
namespace ui {

namespace b = ot::btn;

namespace {
bool isSid(const char* n) {
    const size_t l = std::strlen(n);
    return l > 4 && n[l - 4] == '.' && std::tolower(static_cast<unsigned char>(n[l - 3])) == 's' &&
           std::tolower(static_cast<unsigned char>(n[l - 2])) == 'i' &&
           std::tolower(static_cast<unsigned char>(n[l - 1])) == 'd';
}
}  // namespace

void SidPlayerScreen::enter() {
    // Start where the .sid exports are; fall back to the card root.
    if (!ui().app().platform().storage) return;
    if (!ui().app().platform().storage->exists(path_)) path_[0] = 0;
    refresh();
}

char* SidPlayerScreen::entry(int i) {
    const Platform& p = ui().app().platform();
    if (!p.scratch || p.scratchBytes < kCacheOffset + uint32_t(kMaxEntries) * kRecord) return nullptr;
    return static_cast<char*>(p.scratch) + kCacheOffset + uint32_t(i) * kRecord;
}

void SidPlayerScreen::refresh() {
    // Lists the folder into the cache (once per folder), then fills the window.
    ot::IStorage* fs = ui().app().platform().storage;
    count_ = listed_ = 0;
    if (!fs || !entry(0)) return;
    if (path_[0]) {
        char* e = entry(listed_++);
        e[0] = 1;
        std::snprintf(e + 1, kRecord - 1, "..");
    }
    struct Ctx {
        SidPlayerScreen* s;
        int seen;
    } ctx{this, 0};
    fs->listDir(path_[0] ? path_ : "/",
                [](const ot::IStorage::DirEntry& e, void* c) {
                    Ctx& x = *static_cast<Ctx*>(c);
                    SidPlayerScreen& s = *x.s;
                    if ((++x.seen & 31) == 0) s.ui().app().sidPump();   // a playing tune keeps going
                    if (e.name[0] == '.' || !(e.isDir || isSid(e.name))) return true;
                    if (s.listed_ >= kMaxEntries) return false;
                    char* r = s.entry(s.listed_++);
                    r[0] = e.isDir ? 1 : 0;
                    std::snprintf(r + 1, kRecord - 1, "%s", e.name);
                    return true;
                },
                &ctx);
    count_ = listed_;
    if (sel_ >= count_) sel_ = count_ ? count_ - 1 : 0;
    fill();
}

void SidPlayerScreen::fill() {
    for (int i = 0; i < kRows; ++i) {
        const int k = top_ + i;
        if (k >= count_) break;
        const char* e = entry(k);
        dirs_[i] = e[0] != 0;
        std::snprintf(names_[i], kName, "%s", e + 1);
    }
}

void SidPlayerScreen::move(int d) {
    if (!count_) return;
    sel_ = ((sel_ + d) % count_ + count_) % count_;
    if (sel_ < top_ || sel_ >= top_ + kRows) {
        top_ = sel_ - kRows / 2;
        if (top_ > count_ - kRows) top_ = count_ - kRows;
        if (top_ < 0) top_ = 0;
        fill();
    }
}

void SidPlayerScreen::up() {
    char* slash = std::strrchr(path_, '/');
    if (slash) *slash = 0;
    else path_[0] = 0;
    sel_ = top_ = 0;
    refresh();
}

void SidPlayerScreen::playSelected(int song) {
    if (!count_) return;
    const char* n = names_[sel_ - top_];
    char full[224];
    std::snprintf(full, sizeof full, "%s%s%s", path_, path_[0] ? "/" : "", n);
    App& app = ui().app();
    if (!app.sidPlay(full, song)) ui().message(app.sidError(), 3000);
}

void SidPlayerScreen::activate() {
    if (!count_) return;
    const int i = sel_ - top_;
    if (i < 0 || i >= kRows) return;
    if (dirs_[i]) {
        if (!std::strcmp(names_[i], "..")) {
            up();
            return;
        }
        const size_t l = std::strlen(path_), n = std::strlen(names_[i]);
        if (l + 1 + n + 1 > sizeof path_) {   // too deep for the path buffer
            ui().message("Folder path too long");
            return;
        }
        char* end = path_ + l;
        if (l) *end++ = '/';
        std::memcpy(end, names_[i], n + 1);
        sel_ = top_ = 0;
        refresh();
        return;
    }
    playSelected(0);
}

void SidPlayerScreen::subtune(int d) {
    App& app = ui().app();
    if (!app.sidPlaying() && !app.sidInfo().songs) return;
    const int songs = app.sidInfo().songs ? app.sidInfo().songs : 1;
    const int s = ((app.sidSong() - 1 + d) % songs + songs) % songs + 1;
    char path[160];
    std::snprintf(path, sizeof path, "%s", app.sidPath());
    if (!app.sidPlay(path, s)) ui().message(app.sidError(), 3000);
}

void SidPlayerScreen::encoder(int d) { move(d * (ui().shift ? 16 : 1)); }

bool SidPlayerScreen::button(uint8_t id, bool down) {
    if (!down) return false;
    App& app = ui().app();
    switch (id) {
        case b::Up: move(ui().shift ? -16 : -1); return true;
        case b::Down: move(ui().shift ? 16 : 1); return true;
        case b::Insert:
        case b::EncPush:
        case b::F1: activate(); return true;
        case b::F2: app.sidStop(); return true;
        case b::F3:
        case b::Left: subtune(-1); return true;
        case b::F4:
        case b::Right: subtune(1); return true;
        case b::F5: up(); return true;
        case b::Play:
            if (app.sidPlaying()) app.sidStop();
            else activate();
            return true;
        default: return false;
    }
}

void SidPlayerScreen::labels(const char* out[8]) {
    out[0] = "PLAY";
    out[1] = "STOP";
    out[2] = "SONG -";
    out[3] = "SONG +";
    out[4] = "UP";
}

void SidPlayerScreen::draw(ot::Gfx& g) {
    App& app = ui().app();
    char buf[96];
    std::snprintf(buf, sizeof buf, "/%.90s", path_);
    g.text(16, kContentY + 4, kMono, col::kDim, buf);
    if (!app.platform().storage) {
        g.text(16, kContentY + 40, kText, col::kWarn, "No SD card");
        return;
    }
    // Browser.
    for (int i = 0; i < kRows && top_ + i < count_; ++i) {
        const int y = kContentY + 26 + i * 18;
        const bool sel = top_ + i == sel_;
        if (sel) g.fill(12, y - 1, 380, 18, col::kPanelHi);
        std::snprintf(buf, sizeof buf, "%s%.44s", dirs_[i] ? "/" : " ", names_[i]);
        g.text(20, y, kMono, dirs_[i] ? col::kInst : col::kText, buf);
    }
    if (!count_) g.text(20, kContentY + 26, kMono, col::kDim, "(no .sid files or folders here)");
    // Now playing.
    const int x = 412;
    int y = kContentY + 26;
    const c64::SidInfo& s = app.sidInfo();
    if (!app.sidPlaying()) {
        g.text(x, y, kMono, col::kDim, "STOPPED");
        if (app.sidError()[0]) g.text(x, y + 22, kMono, col::kWarn, app.sidError());
        g.text(x, y + 60, kMono, col::kDim, "Insert / PLAY: play   SONG - / +: subtune");
        g.text(x, y + 78, kMono, col::kDim, "PSID files, one or two SIDs.");
        g.text(x, y + 96, kMono, col::kDim, "HVSC: copy it anywhere on the card.");
        return;
    }
    g.text(x, y, kText, col::kText, s.name);
    g.text(x, y + 24, kMono, col::kInst, s.author);
    g.text(x, y + 42, kMono, col::kDim, s.released);
    const uint32_t sec = app.sidSeconds();
    std::snprintf(buf, sizeof buf, "SONG %d / %u     %u:%02u", app.sidSong(), unsigned(s.songs), unsigned(sec / 60),
                  unsigned(sec % 60));
    g.text(x, y + 72, kText, col::kGood, buf);
    auto model = [](uint8_t m) { return m == 1 ? "6581" : "8580"; };
    if (s.sid2)
        std::snprintf(buf, sizeof buf, "2 SIDs (%s + %s at $%04X), %s", model(s.model[0]), model(s.model[1]),
                      unsigned(s.sid2), s.ntsc ? "NTSC" : "PAL");
    else
        std::snprintf(buf, sizeof buf, "1 SID (%s), %s", model(s.model[0]), s.ntsc ? "NTSC" : "PAL");
    g.text(x, y + 100, kMono, col::kText, buf);
    std::snprintf(buf, sizeof buf, "load $%04X  init $%04X  play $%04X", unsigned(s.load), unsigned(s.init),
                  unsigned(s.play));
    g.text(x, y + 118, kMono, col::kDim, buf);
    // Meters.
    Engine::Meters m = app.engine.takeMeters();
    const float in[3] = {m.sid[0], m.sid[1], m.out};
    const char* names[3] = {"SID1", "SID2", "OUT"};
    const uint32_t colours[3] = {col::kSid1, col::kSid2, col::kGood};
    for (int i = 0; i < 3; ++i) {
        level_[i] *= 0.88f;
        if (in[i] > level_[i]) level_[i] = in[i];
        const int my = y + 152 + i * 26;
        g.text(x, my, kMono, col::kDim, names[i]);
        meter(g, x + 50, my + 2, 300, 12, level_[i] > 1.0f ? 1.0f : level_[i], colours[i]);
    }
}

}  // namespace ui
}  // namespace sidt
