// SIDTracker -- MASTER page (master_screen.h).
#include "ui/master/master_screen.h"

#include <cstdio>

#include "app/app.h"
#include "ui/buttons.h"
#include "ui/ui.h"

namespace sidt {
namespace ui {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

void MasterScreen::encoder(int d) {
    App& app = ui().app();
    Song& s = app.song;
    Engine& e = app.engine;
    switch (sel_) {
        case kVolume: app.setVolume(uint8_t(clampi(app.settings().volume + d * 5, 0, 100))); return;
        case kBalance:
            s.sid.balance = uint8_t(clampi(s.sid.balance + d * 5, 0, 100));
            e.setBalance(s.sid.balance);
            break;
        case kDrums: s.drumVolume = uint8_t(clampi(s.drumVolume + d * 5, 0, 100)); break;
        case kFx: e.setEffects(!e.effects()); return;
        case kDelay: s.fx.delayMix = uint8_t(clampi(s.fx.delayMix + d * 5, 0, 100)); break;
        case kChorus: s.fx.chorusMix = uint8_t(clampi(s.fx.chorusMix + d * 5, 0, 100)); break;
        case kReverb: s.fx.reverbMix = uint8_t(clampi(s.fx.reverbMix + d * 5, 0, 100)); break;
    }
    app.markDirty();
}

bool MasterScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    switch (id) {
        case b::Up: sel_ = (sel_ + kRows - 1) % kRows; return true;
        case b::Down: sel_ = (sel_ + 1) % kRows; return true;
        case b::F1: {
            Engine& e = ui().app().engine;
            e.setEffects(!e.effects());
            ui().message(e.effects() ? "Effects on" : "Effects off");
            return true;
        }
        case b::F8: ui().show(ScreenId::kBench); return true;
        default: return false;
    }
}

void MasterScreen::labels(const char* out[8]) {
    out[0] = ui().app().engine.effects() ? "FX OFF" : "FX ON";
    out[7] = "BENCHMARK";
}

void MasterScreen::draw(ot::Gfx& g) {
    App& app = ui().app();
    const Song& s = app.song;
    const Engine& e = app.engine;
    char v[48];
    const int x = 16, w = 420;
    int y = kContentY + 12;
    auto row = [&](int r, const char* label, const char* value) {
        listRow(g, x, y, w, label, value, sel_ == r);
        y += 30;
    };
    std::snprintf(v, sizeof v, "%u%%", unsigned(app.settings().volume));
    row(kVolume, "MASTER VOLUME", v);
    std::snprintf(v, sizeof v, s.sid.balance ? "%u%%  SID1 L / SID2 R" : "MONO", unsigned(s.sid.balance));
    row(kBalance, "SID BALANCE", v);
    std::snprintf(v, sizeof v, "%u%%", unsigned(s.drumVolume));
    row(kDrums, "DRUM VOLUME", v);
    row(kFx, "EFFECTS", e.effects() ? "ON" : "OFF (all bypassed)");
    const uint8_t mixes[3] = {s.fx.delayMix, s.fx.chorusMix, s.fx.reverbMix};
    const char* names[3] = {"DELAY MIX", "CHORUS MIX", "REVERB MIX"};
    for (int i = 0; i < 3; ++i) {
        if (mixes[i]) std::snprintf(v, sizeof v, "%u%%", unsigned(mixes[i]));
        else std::snprintf(v, sizeof v, "OFF");
        row(kDelay + i, names[i], v);
    }
    g.text(x, y + 8, kMono, col::kDim, "Effect settings: Sample Loader key (EXTENDED)");

    // Meters: peak since the last frame, falling about 20 dB a second.
    Engine::Meters m = app.engine.takeMeters();
    const float in[4] = {m.sid[0], m.sid[1], m.drums, m.out};
    for (int i = 0; i < 4; ++i) {
        level_[i] *= 0.88f;
        if (in[i] > level_[i]) level_[i] = in[i];
    }
    if (m.out >= 0.999f) {
        clip_ = true;
        clipUntil_ = ui().now() + 1500;
    } else if (clip_ && ui().now() > clipUntil_) {
        clip_ = false;
    }
    const char* mnames[4] = {"SID1", "SID2", "DRUMS", "OUT"};
    const uint32_t colours[4] = {col::kSid1, col::kSid2, col::kCmd, col::kGood};
    const int mx = 480, my = kContentY + 20, mh = 260;
    for (int i = 0; i < 4; ++i) {
        const int bx = mx + i * 72;
        g.fill(bx, my, 40, mh, col::kFaint);
        float f = level_[i];
        if (f > 1.0f) f = 1.0f;
        const int h = int(f * float(mh));
        const uint32_t c = i == 3 && clip_ ? col::kBad : colours[i];
        if (h > 0) g.fill(bx, my + mh - h, 40, h, c);
        g.text(bx + 20, my + mh + 8, kSmall, col::kDim, mnames[i], ot::IDisplay::kOptCenter);
    }
    if (clip_) g.text(mx + 3 * 72 + 20, my - 18, kSmall, col::kBad, "CLIP", ot::IDisplay::kOptCenter);
    // CPU (benchmark page window, updated once a second).
    const uint32_t avg = ui().cpuPermille(false), pk = ui().cpuPermille(true);
    if (app.platform().cycleCounter)
        std::snprintf(v, sizeof v, "CPU %u.%u%% avg  %u.%u%% peak", unsigned(avg / 10), unsigned(avg % 10),
                      unsigned(pk / 10), unsigned(pk % 10));
    else
        std::snprintf(v, sizeof v, "CPU n/a (no cycle counter)");
    g.text(mx, my + mh + 40, kMono, pk >= 700 ? col::kWarn : col::kDim, v);
}

}  // namespace ui
}  // namespace sidt
