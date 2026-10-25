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

void MasterScreen::changeLevel(int strip, int d) {
    App& app = ui().app();
    Song& s = app.song;
    if (strip < kDrumStrip) s.mix.volume[strip] = uint8_t(clampi(s.mix.volume[strip] + d * 5, 0, 100));
    else if (strip == kDrumStrip) s.drumVolume = uint8_t(clampi(s.drumVolume + d * 5, 0, 100));
    else {
        app.setVolume(uint8_t(clampi(app.settings().volume + d * 5, 0, 100)));
        return;   // a unit setting, not the song
    }
    app.markDirty();
}

void MasterScreen::encoder(int d) {
    if (page_ == 0) {
        changeLevel(heldF_ >= 0 ? heldF_ : strip_, d);
        if (heldF_ >= 0) jogged_ = true;
        return;
    }
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
    Ui& u = ui();
    if (id == b::Master) {   // Master again: the other page (Shift+Master: benchmark, in Ui)
        if (u.shift) return false;
        if (down) page_ ^= 1;
        heldF_ = -1;
        return true;
    }
    if (page_ == 0 && id <= b::F8) {
        const int k = id - b::F1;
        Engine& e = u.app().engine;
        if (down) {
            heldF_ = k;
            strip_ = k;
            jogged_ = false;
            return true;
        }
        if (heldF_ == k && !jogged_ && k < kMasterStrip) {   // a tap: mute, Shift: solo
            if (u.shift) e.setSolo(k, !e.solo(k));
            else e.setMuted(k, !e.muted(k));
        }
        if (heldF_ == k) heldF_ = -1;
        return true;
    }
    if (!down) return false;
    if (page_ == 0) {
        switch (id) {
            case b::Left: strip_ = (strip_ + kStrips - 1) % kStrips; return true;
            case b::Right: strip_ = (strip_ + 1) % kStrips; return true;
            case b::Delete: {
                App& app = u.app();
                if (strip_ < kDrumStrip) app.song.mix.volume[strip_] = 100;
                else if (strip_ == kDrumStrip) app.song.drumVolume = 80;
                else {
                    app.setVolume(80);
                    return true;
                }
                app.markDirty();
                return true;
            }
            default: return false;
        }
    }
    switch (id) {
        case b::Up: sel_ = (sel_ + kRows - 1) % kRows; return true;
        case b::Down: sel_ = (sel_ + 1) % kRows; return true;
        case b::F1: {
            Engine& e = u.app().engine;
            e.setEffects(!e.effects());
            u.status(e.effects() ? "Effects on" : "Effects off");
            return true;
        }
        case b::F8: u.show(ScreenId::kBench); return true;
        default: return false;
    }
}

void MasterScreen::labels(const char* out[8]) {
    if (page_ == 0) {
        static const char* const kNames[kStrips] = {"TRK 1", "TRK 2", "TRK 3", "TRK 4", "TRK 5", "TRK 6", "DRUMS", "MASTER"};
        for (int i = 0; i < kStrips; ++i) out[i] = kNames[i];
        return;
    }
    out[0] = ui().app().engine.effects() ? "FX OFF" : "FX ON";
    out[7] = "BENCHMARK";
}

void MasterScreen::drawMixer(ot::Gfx& g) {
    App& app = ui().app();
    const Song& s = app.song;
    const Engine& e = app.engine;
    Engine::Meters m = app.engine.takeMeters();
    char v[24];
    const int top = kContentY + 36, fh = 250;
    g.text(16, kContentY + 6, kMono, col::kDim,
           "MIXER  hold a button below + jog: level   tap: mute   Shift+tap: solo   Master: page 2");
    for (int i = 0; i < kStrips; ++i) {
        const int x = i * 100 + 10, w = 80;
        float level, meterIn;
        uint32_t c;
        if (i < kDrumStrip) {
            level = float(s.mix.volume[i]) / 100.0f;
            const bool silent = e.muted(i) || (e.anySolo() && !e.solo(i));
            meterIn = silent ? 0.0f : float(e.chip(i / 3).envLevel(i % 3)) / 255.0f * level;
            c = i < 3 ? col::kSid1 : col::kSid2;
        } else if (i == kDrumStrip) {
            level = float(s.drumVolume) / 100.0f;
            meterIn = m.drums;
            c = col::kCmd;
        } else {
            level = float(app.settings().volume) / 100.0f;
            meterIn = m.out;
            c = col::kGood;
        }
        stripLevel_[i] *= 0.85f;
        if (meterIn > stripLevel_[i]) stripLevel_[i] = meterIn;
        const bool sel = i == strip_;
        g.fill(x - 4, top - 4, w + 8, fh + 64, sel ? col::kPanelHi : col::kPanel);
        // Fader (level) on the left, meter on the right.
        g.fill(x + 8, top, 24, fh, col::kFaint);
        const int lh = int(level * float(fh));
        if (lh > 0) g.fill(x + 8, top + fh - lh, 24, lh, sel ? col::kCursor : col::kText);
        g.fill(x + 44, top, 24, fh, col::kFaint);
        float f = stripLevel_[i] > 1.0f ? 1.0f : stripLevel_[i];
        const int mh = int(f * float(fh));
        if (mh > 0) g.fill(x + 44, top + fh - mh, 24, mh, c);
        std::snprintf(v, sizeof v, "%d%%", int(level * 100.0f + 0.5f));
        g.text(x + w / 2, top + fh + 8, kMono, col::kText, v, ot::IDisplay::kOptCenter);
        if (i < kMasterStrip) {
            const bool mu = e.muted(i), so = e.solo(i);
            g.text(x + w / 2, top + fh + 30, kSmall, mu ? col::kBad : (so ? col::kWarn : col::kFaint),
                   mu ? "MUTE" : (so ? "SOLO" : "-"), ot::IDisplay::kOptCenter);
        }
    }
}

void MasterScreen::draw(ot::Gfx& g) {
    if (page_ == 0) {
        drawMixer(g);
        return;
    }
    drawSettings(g);
}

void MasterScreen::drawSettings(ot::Gfx& g) {
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
