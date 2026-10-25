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
        case kSidCopy:
            u.app().setSidCopy(!u.app().settings().sidCopy);
            return;   // a unit setting
        case kVolume:
            u.app().setVolume(uint8_t(clampi(u.app().settings().volume + d * 5, 0, 100)));
            return;   // not part of the song
        case kMidiCh:
        case kClockOut:
        case kSyncIn:
        case kAsidOut: {
            App& a = u.app();
            App::Settings st = a.settings();
            if (sel_ == kMidiCh) st.midiChannel = uint8_t(((st.midiChannel + d) % 17 + 17) % 17);
            else if (sel_ == kClockOut) st.clockOut = uint8_t(((st.clockOut + d) % 4 + 4) % 4);
            else if (sel_ == kSyncIn) st.syncIn = uint8_t(((st.syncIn + d) % 3 + 3) % 3);
            else st.asidOut = uint8_t(((st.asidOut + d) % 4 + 4) % 4);
            a.setMidi(st.midiChannel, st.clockOut, st.syncIn, st.asidOut);
            return;   // unit settings, not the song
        }
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
           
            return;
    }
    u.app().markDirty();
}

bool SettingsScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    switch (id) {
        case b::Up: sel_ = (sel_ + kRowCount - 1) % kRowCount; cursor_ = 0; return true;
        case b::Down: sel_ = (sel_ + 1) % kRowCount; cursor_ = 0; return true;
        case b::Left: if (cursor_ > 0) --cursor_; return true;
        case b::Right: if (cursor_ < kNameLen - 1) ++cursor_; return true;
        case b::F7:
            sel_ = kFirmware;
           
            return true;
        case b::F8:
        case b::Insert:
        case b::EncPush: {
            if ((sel_ == kName || sel_ == kAuthor) && id != b::F8) {   // the pad keyboard
                Song& sg = ui().song();
                char* field = sel_ == kName ? sg.name : sg.author;
                if (const char* t = ui().keyboardResult(id)) {
                    std::snprintf(field, kNameLen + 1, "%s", t);
                    ui().app().markDirty();
                } else {
                    ui().keyboard(this, id, sel_ == kName ? "SONG NAME" : "AUTHOR", field);
                }
                return true;
            }
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
            {
                char q[72];
                std::snprintf(q, sizeof q, "Install %s? The Tracker restarts.", images_[image_]);
                if (!ui().confirm(this, id, q, "INSTALL")) return true;
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
    if (sel_ == kFirmware) out[7] = "INSTALL";
}

void SettingsScreen::draw(ot::Gfx& g) {
    const Song& s = ui().song();
    char v[80];
    const int x = 40, w = 720;
    int y = kContentY + 8;
    auto row = [&](int r, const char* label, const char* value) {
        listRow(g, x, y, w, label, value, sel_ == r);
        y += 21;
    };
    row(kSid1, "SID1 MODEL", s.sid.model[0] == sid::Model::k6581 ? "6581" : "8580");
    row(kSid2, "SID2 MODEL", s.sid.model[1] == sid::Model::k6581 ? "6581" : "8580");
    row(kClock, "SID CLOCK", s.sid.clock == SidClock::kPal ? "PAL 985248 Hz" : "NTSC 1022727 Hz");
    std::snprintf(v, sizeof v, "%u%%", unsigned(s.sid.balance));
    row(kBalance, "SID BALANCE", v);
    {
        const App& a = ui().app();
        const char* cv = !a.settings().sidCopy ? "OFF"
                         : a.sidCopyActive()   ? "ON  (copying now)"
                                               : "ON  (SID 2 in use: off)";
        row(kSidCopy, "ONE SID ON BOTH SIDES", cv);
    }
    std::snprintf(v, sizeof v, "%u%%", unsigned(ui().app().settings().volume));
    row(kVolume, "MASTER VOLUME", v);
    row(kTheme, "THEME", theme(ui().app().settings().theme).name);
    {
        const App& a = ui().app();
        const App::Settings& st = a.settings();
        static const char* const kPorts[4] = {"OFF", "DIN", "USB", "DIN + USB"};
        if (st.midiChannel) std::snprintf(v, sizeof v, "CHANNEL %u", unsigned(st.midiChannel));
        else std::snprintf(v, sizeof v, "ALL CHANNELS");
        row(kMidiCh, "MIDI IN (notes, CC)", v);
        row(kClockOut, "MIDI CLOCK OUT", kPorts[st.clockOut & 3]);
        if (st.syncIn && a.midiSynced())
            std::snprintf(v, sizeof v, "%s  locked %u.%u bpm", kPorts[st.syncIn], unsigned(a.midiSyncBpmX10() / 10),
                          unsigned(a.midiSyncBpmX10() % 10));
        else if (st.syncIn)
            std::snprintf(v, sizeof v, "%s  (no clock)", kPorts[st.syncIn]);
        else
            std::snprintf(v, sizeof v, "OFF (own tempo)");
        row(kSyncIn, "MIDI SYNC IN", v);
        row(kAsidOut, "ASID OUT (real SID)", kPorts[st.asidOut & 3]);
    }
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
        const int cy = kContentY + 8 + sel_ * 21 + 17;
        g.hline(x + w - 8 - (len - cursor_) * 8, cy, 8, col::kCursor);
    }
    if (!ui().app().platform().requestFirmwareUpdate) std::snprintf(v, sizeof v, "not available on this platform");
    else if (!imageCount_) std::snprintf(v, sizeof v, "no images in /Firmware");
    else std::snprintf(v, sizeof v, "%s", images_[image_]);
    row(kFirmware, "FIRMWARE UPDATE", v);
    // One line of help for the selected row (UX plan P2.5).
    static const char* const kHelp[kRowCount] = {
        "Chip type of SID 1 (tracks 1-3): 6581 darker filter, 8580 cleaner",
        "Chip type of SID 2 (tracks 4-6)",
        "PAL or NTSC C64 clock: changes pitch slightly and the .sid header",
        "Stereo: 0 mono, 100 SID 1 left / SID 2 right",
        "A one-SID song or .sid tune also plays on SID 2: both sides (and both real chips)",
        "Output level of the Tracker (not saved in the song)",
        "Screen colours",
        "MIDI notes play the pads' instrument; CC 74 cutoff, 71 resonance",
        "Send MIDI clock (6 per row) and Start / Stop",
        "Follow an external MIDI clock: tempo and Start / Stop / Continue",
        "SID register frames for real chips (USBSID-Pico etc.)",
        "EXTENDED allows drums, effects, LFOs, mixer (not in GT / .sid exports)",
        "Attack / decay written before a new note (hard restart)",
        "Sustain / release written before a new note (hard restart)",
        "Jog: letter, Left / Right: position",
        "Jog: letter, Left / Right: position",
        "Jog: choose an image in /Firmware, then INSTALL (asks first)",
    };
    g.text(40, kBarY - 22, kMono, col::kDim, kHelp[sel_]);
}

}  // namespace ui
}  // namespace sidt
