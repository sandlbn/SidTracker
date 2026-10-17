// SIDTracker -- Settings screen (settings_screen.h).
#include "ui/settings/settings_screen.h"

#include "ui/palette.h"

#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
const char kChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._#+";
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void editNameChar(char* name, int pos, int d) {
    if (pos < 0 || pos >= kNameLen) return;
    for (int i = 0; i < pos; ++i)
        if (!name[i]) name[i] = ' ';
    const char* p = std::strchr(kChars, name[pos] ? name[pos] : ' ');
    const int n = int(sizeof kChars) - 1;
    int i = p ? int(p - kChars) : 0;
    i = ((i + d) % n + n) % n;
    name[pos] = kChars[i];
    name[kNameLen] = 0;
    // Trim trailing spaces so the stored name stays clean.
    for (int k = kNameLen - 1; k >= 0 && (name[k] == ' ' || !name[k]); --k)
        if (k > pos) name[k] = 0;
}

void SettingsScreen::enter() {
    confirm_ = false;
    scanImages();
}

void SettingsScreen::scanImages() {
    imageCount_ = 0;
    image_ = 0;
    const Platform& p = ui().app().platform();
    if (!p.storage || !p.isUpdateImageName) return;
    struct Ctx {
        SettingsScreen* s;
        bool (*ok)(const char*);
    } ctx{this, p.isUpdateImageName};
    p.storage->listDir("Firmware",
                       [](const ot::IStorage::DirEntry& e, void* c) {
                           Ctx& x = *static_cast<Ctx*>(c);
                           if (e.isDir || !x.ok(e.name) || x.s->imageCount_ >= kMaxImages) return true;
                           std::snprintf(x.s->images_[x.s->imageCount_++], 64, "%s", e.name);
                           return true;
                       },
                       &ctx);
}

void SettingsScreen::encoder(int d) {
    Ui& u = ui();
    Song& s = u.song();
    Engine& e = u.app().engine;
    const int big = u.shift ? 16 : 1;
    switch (sel_) {
        case kSid1:
        case kSid2: {
            const int chip = sel_ - kSid1;
            s.sid.model[chip] = s.sid.model[chip] == sid::Model::k6581 ? sid::Model::k8580 : sid::Model::k6581;
            e.setModel(chip, s.sid.model[chip]);
            break;
        }
        case kClock:
            s.sid.clock = s.sid.clock == SidClock::kPal ? SidClock::kNtsc : SidClock::kPal;
            e.setClock(s.sid.clock);
            break;
        case kBalance:
            s.sid.balance = uint8_t(clampi(s.sid.balance + d * 5, 0, 100));
            e.setBalance(s.sid.balance);
            break;
        case kVolume:
            u.app().setVolume(uint8_t(clampi(u.app().settings().volume + d * 5, 0, 100)));
            return;   // not part of the song
        case kTheme: {
            const int n = themeCount();
            u.app().setTheme(uint8_t(((u.app().settings().theme + d) % n + n) % n));
            return;
        }
        case kMode: s.mode = s.mode == SongMode::kAuthentic ? SongMode::kExtended : SongMode::kAuthentic; break;
        case kHrAD: s.hrAD = uint8_t(s.hrAD + d * big); break;
        case kHrSR: s.hrSR = uint8_t(s.hrSR + d * big); break;
        case kName: editNameChar(s.name, cursor_, d); break;
        case kAuthor: editNameChar(s.author, cursor_, d); break;
        case kFirmware:
            if (imageCount_) image_ = (image_ + d % imageCount_ + imageCount_) % imageCount_;
            confirm_ = false;
            return;
    }
    u.app().markDirty();
}

bool SettingsScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    switch (id) {
        case b::Up: sel_ = (sel_ + kRowCount - 1) % kRowCount; cursor_ = 0; confirm_ = false; return true;
        case b::Down: sel_ = (sel_ + 1) % kRowCount; cursor_ = 0; confirm_ = false; return true;
        case b::Left: if (cursor_ > 0) --cursor_; return true;
        case b::Right: if (cursor_ < kNameLen - 1) ++cursor_; return true;
        case b::F7:
            sel_ = kFirmware;
            confirm_ = false;
            return true;
        case b::F8:
        case b::Insert:
        case b::EncPush: {
            if (sel_ != kFirmware) return id != b::F8;
            const Platform& p = ui().app().platform();
            if (!p.requestFirmwareUpdate || !p.storage) {
                ui().message("Firmware update: not available here");
                return true;
            }
            if (!imageCount_) {
                ui().message("No update images in /Firmware");
                return true;
            }
            if (!confirm_) {
                confirm_ = true;
                ui().message("Press INSTALL again to flash it", 3000);
                return true;
            }
            ui().app().autosave();
            if (!p.requestFirmwareUpdate(*p.storage, images_[image_])) ui().message("Update request failed", 3000);
            return true;
        }
        default: return false;
    }
}

void SettingsScreen::labels(const char* out[8]) {
    out[6] = "FIRMWARE";
    if (sel_ == kFirmware) out[7] = confirm_ ? "CONFIRM" : "INSTALL";
}

void SettingsScreen::draw(ot::Gfx& g) {
    const Song& s = ui().song();
    char v[80];
    const int x = 40, w = 720;
    int y = kContentY + 8;
    auto row = [&](int r, const char* label, const char* value) {
        listRow(g, x, y, w, label, value, sel_ == r);
        y += 30;
    };
    row(kSid1, "SID1 MODEL", s.sid.model[0] == sid::Model::k6581 ? "6581" : "8580");
    row(kSid2, "SID2 MODEL", s.sid.model[1] == sid::Model::k6581 ? "6581" : "8580");
    row(kClock, "SID CLOCK", s.sid.clock == SidClock::kPal ? "PAL 985248 Hz" : "NTSC 1022727 Hz");
    std::snprintf(v, sizeof v, "%u%%", unsigned(s.sid.balance));
    row(kBalance, "SID BALANCE", v);
    std::snprintf(v, sizeof v, "%u%%", unsigned(ui().app().settings().volume));
    row(kVolume, "MASTER VOLUME", v);
    row(kTheme, "THEME", theme(ui().app().settings().theme).name);
    row(kMode, "SONG MODE", s.mode == SongMode::kAuthentic ? "AUTHENTIC (GoatTracker compatible)" : "EXTENDED");
    std::snprintf(v, sizeof v, "%02X", unsigned(s.hrAD));
    row(kHrAD, "HARD RESTART AD", v);
    std::snprintf(v, sizeof v, "%02X", unsigned(s.hrSR));
    row(kHrSR, "HARD RESTART SR", v);
    row(kName, "SONG NAME", s.name);
    row(kAuthor, "AUTHOR", s.author[0] ? s.author : " ");
    if (sel_ == kName || sel_ == kAuthor) {
        const char* t = sel_ == kName ? s.name : s.author;
        const int len = int(std::strlen(t));
        const int cy = kContentY + 8 + sel_ * 30 + 17;
        g.hline(x + w - 8 - (len - cursor_) * 8, cy, 8, col::kCursor);
    }
    if (!ui().app().platform().requestFirmwareUpdate) std::snprintf(v, sizeof v, "not available on this platform");
    else if (!imageCount_) std::snprintf(v, sizeof v, "no images in /Firmware");
    else std::snprintf(v, sizeof v, "%s%s", images_[image_], confirm_ ? "  (INSTALL again)" : "");
    row(kFirmware, "FIRMWARE UPDATE", v);
}

}  // namespace ui
}  // namespace sidt
