// SIDTracker -- Extended screen (extended_screen.h).
#include "ui/extended/extended_screen.h"

#include <cstdio>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct FxRow {
    const char* name;
    uint8_t audio::FxSettings::*field;
    int lo, hi;
    const char* unit;
};
const FxRow kFx[] = {
    {"DELAY MIX", &audio::FxSettings::delayMix, 0, 100, "%"},
    {"DELAY TIME", &audio::FxSettings::delayTicks, 1, 64, " TICKS"},
    {"DELAY FEEDBACK", &audio::FxSettings::delayFeedback, 0, 90, "%"},
    {"CHORUS MIX", &audio::FxSettings::chorusMix, 0, 100, "%"},
    {"CHORUS RATE", &audio::FxSettings::chorusRate, 1, 100, ""},
    {"CHORUS DEPTH", &audio::FxSettings::chorusDepth, 0, 100, ""},
    {"REVERB MIX", &audio::FxSettings::reverbMix, 0, 100, "%"},
    {"REVERB SIZE", &audio::FxSettings::reverbSize, 0, 100, ""},
    {"REVERB DAMPING", &audio::FxSettings::reverbDamp, 0, 100, ""},
};
constexpr int kFxRows = int(sizeof kFx / sizeof kFx[0]);
}  // namespace

int ExtendedScreen::rows() const {
    switch (page_) {
        case kEffects: return kFxRows;
        case kKit: return 1 + audio::kKitSlots;
        default: return modRows();
    }
}

void ExtendedScreen::encoder(int d) {
    Song& s = ui().song();
    const int step = ui().shift ? 10 : 1;
    if (page_ == kEffects) {
        const FxRow& r = kFx[sel_];
        uint8_t& v = s.fx.*(r.field);
        v = uint8_t(clampi(v + d * step, r.lo, r.hi));
    } else if (page_ == kKit) {
        if (sel_ == 0) s.drumVolume = uint8_t(clampi(s.drumVolume + d * step, 0, 100));
        else return;
    } else {
        modEdit(d * step);
    }
    ui().app().markDirty();
}

bool ExtendedScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    if (page_ != kKit) return false;
    if (down) ui().app().engine.drumPreview(uint8_t(p % 16 + 1));
    return true;
}

bool ExtendedScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Engine& e = ui().app().engine;
    switch (id) {
        case b::F1: page_ = kEffects; sel_ = 0; return true;
        case b::F2: page_ = kKit; sel_ = 0; return true;
        case b::F3: page_ = kModulation; sel_ = 0; return true;
        case b::F5:
            if (page_ == kKit) {
                char msg[48];
                std::snprintf(msg, sizeof msg, "Kit reloaded: %d WAV files", ui().app().loadKit());
                ui().message(msg);
            }
            return true;
        case b::F8:
            e.setEffects(!e.effects());
            ui().status(e.effects() ? "Effects on" : "Effects off (all songs)");
            return true;
        case b::Up: sel_ = (sel_ + rows() - 1) % rows(); return true;
        case b::Down: sel_ = (sel_ + 1) % rows(); return true;
        case b::Insert:
        case b::EncPush:
            if (page_ == kKit && sel_ > 0) e.drumPreview(uint8_t(sel_));
            return true;
        default: return false;
    }
}

void ExtendedScreen::labels(const char* out[8]) {
    out[0] = "EFFECTS";
    out[1] = "DRUM KIT";
    out[2] = "MODULATION";
    if (page_ == kKit) out[4] = "RELOAD KIT";
    out[7] = ui().app().engine.effects() ? "FX ON" : "FX OFF";
}

void ExtendedScreen::drawEffects(ot::Gfx& g) {
    const Song& s = ui().song();
    char v[24];
    for (int i = 0; i < kFxRows; ++i) {
        const FxRow& r = kFx[i];
        const uint8_t val = s.fx.*(r.field);
        if (val == 0 && r.lo == 0 && (i % 3) == 0) std::snprintf(v, sizeof v, "OFF");
        else std::snprintf(v, sizeof v, "%u%s", unsigned(val), r.unit);
        listRow(g, 40, kContentY + 34 + i * 28, 520, r.name, v, sel_ == i);
    }
    const bool on = ui().app().engine.effects();
    g.text(590, kContentY + 34, kMono, on ? col::kGood : col::kWarn, on ? "EFFECTS ON" : "EFFECTS OFF (FX button)");
    g.text(590, kContentY + 56, kMono, col::kDim, "delay -> chorus -> reverb");
    g.text(590, kContentY + 74, kMono, col::kDim, "on SIDs and drums");
}

void ExtendedScreen::drawKit(ot::Gfx& g) {
    const Song& s = ui().song();
    const audio::Kit& kit = ui().app().engine.kit();
    char v[40];
    std::snprintf(v, sizeof v, "%u%%", unsigned(s.drumVolume));
    listRow(g, 40, kContentY + 34, 520, "DRUM VOLUME", v, sel_ == 0);
    const int top = sel_ > 12 ? sel_ - 12 : 0;
    for (int i = 0; i < 12 && top + i < audio::kKitSlots; ++i) {
        const int slot = top + i + 1;
        const audio::Sample& smp = kit.slot(slot);
        if (smp.data) {
            std::snprintf(v, sizeof v, "%-12s %4u ms", smp.name, unsigned(smp.frames * 1000u / smp.rateHz));
        } else {
            std::snprintf(v, sizeof v, "(empty)");
        }
        char label[16];
        std::snprintf(label, sizeof label, "SLOT %02X", unsigned(slot));
        listRow(g, 40, kContentY + 66 + i * 26, 520, label, v, sel_ == slot);
    }
    g.text(590, kContentY + 34, kMono, col::kDim, "pads 1-16: audition");
    g.text(590, kContentY + 52, kMono, col::kDim, "card: SIDTracker/Kit/");
    g.text(590, kContentY + 70, kMono, col::kDim, "  01KICK.wav .. 16xx.wav");
    std::snprintf(v, sizeof v, "memory %u / %u KB", unsigned(kit.usedSamples() / 512), unsigned(kit.capacity() / 512));
    g.text(590, kContentY + 94, kMono, col::kDim, v);
}

void ExtendedScreen::modEdit(int d) {
    ModSettings& m = ui().song().mod;
    if (sel_ < 8) {
        Lfo& l = m.lfo[sel_ / 4];
        switch (sel_ % 4) {
            case 0: l.target = uint8_t(clampi(l.target + (d > 0 ? 1 : -1), 0, 8)); break;
            case 1: l.shape = uint8_t((l.shape + (d > 0 ? 1 : Lfo::kShapes - 1)) % Lfo::kShapes); break;
            case 2: l.period = uint8_t(clampi(l.period + d, 2, 255)); break;
            default: l.depth = uint8_t(clampi(l.depth + d, 0, 100)); break;
        }
    } else {
        int8_t& p = m.pan[sel_ - 8];
        p = int8_t(clampi(p + d * 5, -100, 100));
    }
}

void ExtendedScreen::drawModulation(ot::Gfx& g) {
    const ModSettings& m = ui().song().mod;
    char v[32];
    int y = kContentY + 34;
    for (int k = 0; k < 2; ++k) {
        const Lfo& l = m.lfo[k];
        char label[24];
        std::snprintf(label, sizeof label, "LFO %d TARGET", k + 1);
        listRow(g, 40, y, 360, label, lfoTargetName(l.target), sel_ == k * 4);
        y += 26;
        listRow(g, 40, y, 360, "  SHAPE", lfoShapeName(l.shape), sel_ == k * 4 + 1);
        y += 26;
        std::snprintf(v, sizeof v, "%u TICKS", unsigned(l.period));
        listRow(g, 40, y, 360, "  PERIOD", v, sel_ == k * 4 + 2);
        y += 26;
        std::snprintf(v, sizeof v, "%u", unsigned(l.depth));
        listRow(g, 40, y, 360, "  DEPTH", l.depth ? v : "OFF", sel_ == k * 4 + 3);
        y += 34;
    }
    y = kContentY + 34;
    for (int ch = 0; ch < kChannels; ++ch) {
        char label[24];
        std::snprintf(label, sizeof label, "PAN VOICE %d", ch + 1);
        const int p = m.pan[ch];
        if (!p) std::snprintf(v, sizeof v, "SID BALANCE");
        else std::snprintf(v, sizeof v, "%s %d", p < 0 ? "L" : "R", p < 0 ? -p : p);
        listRow(g, 420, y, 360, label, v, sel_ == 8 + ch);
        y += 26;
    }
    g.text(420, y + 10, kMono, col::kDim, "filtered voices follow their SID");
}

void ExtendedScreen::draw(ot::Gfx& g) {
    static const char* const kTitles[kPages] = {"EFFECTS", "DRUM KIT", "MODULATION"};
    g.text(16, kContentY + 4, kLarge, col::kText, kTitles[page_]);
    if (ui().song().extendedFeatures()) g.text(600, kContentY + 8, kMono, col::kWarn, "not in GT export");
    switch (page_) {
        case kEffects: drawEffects(g); break;
        case kKit: drawKit(g); break;
        default: drawModulation(g); break;
    }
}

}  // namespace ui
}  // namespace sidt
