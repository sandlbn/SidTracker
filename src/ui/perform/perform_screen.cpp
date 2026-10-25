// SIDTracker -- Perform screen (perform_screen.h).
#include "ui/perform/perform_screen.h"

#include <cstdio>

#include "app/app.h"
#include "sid/regs.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
// Waveform override cycle: song, triangle, saw, pulse, noise.
const uint8_t kWaves[5] = {0, sid::kTri, sid::kSaw, sid::kPulse, sid::kNoise};
const char* const kWaveNames[5] = {"SONG", "TRI", "SAW", "PUL", "NOI"};
int waveIndex(uint8_t w) {
    for (int i = 0; i < 5; ++i)
        if (kWaves[i] == w) return i;
    return 0;
}
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

int PerformScreen::laneOf(int row, int ctl) {
    if (row < 6) return ctl == 2 ? kAutoWave + row : (ctl == 3 ? kAutoPw + row : -1);
    if (row < 8) return ctl == 0 ? kAutoCutoff + row - 6 : kAutoRes + row - 6;
    return -1;
}

bool PerformScreen::recording() { return ui().edit && ui().app().engine.player().playing(); }

int PerformScreen::currentPattern() {
    const Player& pl = ui().app().engine.player();
    return pl.playing() ? pl.patternIndex() : ui().pattern;
}

void PerformScreen::record(int lane) {
    if (lane < 0 || !recording()) return;
    const Player& pl = ui().app().engine.player();
    Pattern& p = ui().song().pattern(pl.patternIndex());
    const int row = pl.row() < p.rows ? pl.row() : 0;
    p.setAuto(row, lane, pl.performLane(lane));
    touched_[lane] = ui().now() ? ui().now() : 1;
    lastPos_[lane] = pl.patternIndex() * 256 + row;
    ui().app().markDirty();
}

void PerformScreen::change(int row, int ctl, int d, bool toggle) {
    Engine& e = ui().app().engine;
    PerformOverrides& o = e.playerMutable().perform;
    if (row < 6) {
        switch (ctl) {
            case 0: e.setMuted(row, toggle ? !e.muted(row) : d > 0); break;
            case 1: e.setSolo(row, toggle ? !e.solo(row) : d > 0); break;
            case 2: {
                const int i = toggle ? 0 : clampi(waveIndex(o.wave[row]) + d, 0, 4);
                o.wave[row] = kWaves[i];
                break;
            }
            case 3: o.pwOffset[row] = int16_t(toggle ? 0 : clampi(o.pwOffset[row] + d * 0x40, -0xFFF, 0xFFF)); break;
        }
    } else if (row < 8) {
        const int chip = row - 6;
        if (ctl == 0) o.cutoffOffset[chip] = int16_t(toggle ? 0 : clampi(o.cutoffOffset[chip] + d * 32, -2047, 2047));
        else o.resonance[chip] = int8_t(toggle ? -1 : clampi(o.resonance[chip] + d, -1, 15));
    } else {
        const int w = toggle ? 50 : clampi(ui().song().sid.balance + d * 5, 0, 100);
        ui().song().sid.balance = uint8_t(w);
        e.setBalance(uint8_t(w));
        ui().app().markDirty();   // song data (not a recorded lane)
    }
    record(laneOf(row, ctl));
}

void PerformScreen::encoder(int d) { change(row_, ctl_, d, false); }

bool PerformScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    switch (id) {
        case b::Up: row_ = (row_ + kRowsCount - 1) % kRowsCount; ctl_ = clampi(ctl_, 0, controls(row_) - 1); return true;
        case b::Down: row_ = (row_ + 1) % kRowsCount; ctl_ = clampi(ctl_, 0, controls(row_) - 1); return true;
        case b::Left: ctl_ = (ctl_ + controls(row_) - 1) % controls(row_); return true;
        case b::Right: ctl_ = (ctl_ + 1) % controls(row_); return true;
        case b::Insert:
        case b::EncPush: change(row_, ctl_, 0, true); return true;
        case b::F2: {
            if (row_ >= 8) return true;
            if (!ui().confirming(b::F2)) clearPattern_ = currentPattern();   // the pattern asked about
            if (!ui().confirm(this, b::F2, "Clear this row's automation in this pattern?", "CLEAR")) return true;
            const int a = laneOf(row_, row_ < 6 ? 2 : 0), c = laneOf(row_, row_ < 6 ? 3 : 1);
            ui().song().pattern(clearPattern_).clearAuto(uint16_t((1u << a) | (1u << c)));
            ui().app().markDirty();
            ui().status("Automation cleared");
            return true;
        }
        case b::F1: {
            Engine& e = ui().app().engine;
            e.playerMutable().perform = PerformOverrides{};
            for (int ch = 0; ch < kChannels; ++ch) {
                e.setMuted(ch, false);
                e.setSolo(ch, false);
            }
            ui().status("Overrides cleared");
            return true;
        }
        default: return false;
    }
}

bool PerformScreen::pad(int p, bool down, uint8_t vel) {
    (void)vel;
    const int r = p / 12, c = p % 12;
    if (r == 3 || c >= kChannels) return false;   // bottom row / right side audition
    if (!down) return true;
    if (r == 2) {   // cycle the waveform override, wrapping back to the song
        PerformOverrides& o = ui().app().engine.playerMutable().perform;
        o.wave[c] = kWaves[(waveIndex(o.wave[c]) + 1) % 5];
        record(kAutoWave + c);
    } else {
        change(c, r, 0, true);   // toggle mute (row 0) / solo (row 1)
    }
    return true;
}

bool PerformScreen::padColour(int p, uint32_t& rgb) {
    const int r = p / 12, c = p % 12;
    if (r == 3 || c >= kChannels) return false;
    const Engine& e = ui().app().engine;
    if (r == 0) rgb = e.muted(c) ? 0xFF2020 : 0x202020;
    else if (r == 1) rgb = e.solo(c) ? 0xFFC000 : 0x202020;
    else rgb = e.player().perform.wave[c] ? 0x2080FF : 0x101830;
    return true;
}

void PerformScreen::labels(const char* out[8]) {
    out[0] = "CLEAR";
    out[1] = "CLR AUTO";
}

void PerformScreen::drawVoice(ot::Gfx& g, int ch, int x, int y) {
    const Engine& e = ui().app().engine;
    const Player& pl = e.player();
    const sid::Chip& chip = e.chip(ch / 3);
    const bool sel = row_ == ch;
    const uint32_t chipCol = ch < 3 ? col::kSid1 : col::kSid2;
    g.fill(x, y, 380, 70, sel ? col::kPanelHi : col::kPanel);
    char buf[48];
    std::snprintf(buf, sizeof buf, "VOICE %d", ch + 1);
    g.text(x + 8, y + 6, kText, chipCol, buf);
    char n[4];
    noteName(uint8_t(pl.channelNote(ch) + 1), n);
    g.text(x + 8, y + 30, kMono, pl.channelGate(ch) ? col::kText : col::kDim, n);
    const PerformOverrides& o = pl.perform;
    const char* items[4];
    char wave[16], pw[16];
    std::snprintf(wave, sizeof wave, "WAVE %s", kWaveNames[waveIndex(o.wave[ch])]);
    std::snprintf(pw, sizeof pw, "PW %+d", int(o.pwOffset[ch]));
    items[0] = "M";
    items[1] = "S";
    items[2] = wave;
    items[3] = pw;
    const int xs[4] = {120, 150, 190, 290};
    const uint16_t autoHere = ui().song().pattern(currentPattern()).autoLanes();
    for (int i = 0; i < 4; ++i) {
        const bool on = (i == 0 && e.muted(ch)) || (i == 1 && e.solo(ch)) || (i == 2 && o.wave[ch]) ||
                        (i == 3 && o.pwOffset[ch]);
        const uint32_t c = i == 0 && on ? col::kBad : (i == 1 && on ? col::kWarn : (on ? col::kText : col::kDim));
        g.text(x + xs[i], y + 6, kMono, c, items[i]);
        const int lane = laneOf(ch, i);
        if (lane >= 0 && ((autoHere >> lane) & 1)) g.text(x + xs[i] - 9, y + 6, kMono, col::kEdit, "*");
        if (sel && ctl_ == i) g.hline(x + xs[i], y + 23, i < 2 ? 8 : 80, col::kCursor);
    }
    // Envelope and pulse-width meters.
    meter(g, x + 120, y + 34, 250, 10, float(chip.envLevel(ch % 3)) / 255.0f, chipCol);
    meter(g, x + 120, y + 50, 250, 8, float(pl.channelPw(ch)) / 4095.0f, col::kDim);
}

void PerformScreen::draw(ot::Gfx& g) {
    const Engine& e = ui().app().engine;
    const Player& pl = e.player();
    // Latch: a control turned within the last 700 ms records on every row.
    const bool rec = recording();
    for (int lane = 0; lane < kAutoLanes; ++lane) {
        if (!touched_[lane]) continue;
        if (!rec || ui().now() - touched_[lane] > 700) {
            touched_[lane] = 0;
            continue;
        }
        const int pos = pl.patternIndex() * 256 + pl.row();
        if (pos != lastPos_[lane]) {
            const uint32_t keep = touched_[lane];
            record(lane);
            touched_[lane] = keep;   // only the knob extends the latch
        }
    }
    const Song& s = ui().song();
    if (rec) g.text(240, kContentY + 2, kText, col::kEdit, "REC AUTOMATION");
    else if (s.pattern(currentPattern()).autoLanes()) g.text(240, kContentY + 2, kText, col::kDim, "AUTOMATION *");
    g.text(16, kContentY + 2, kText, col::kSid1, s.sid.model[0] == sid::Model::k6581 ? "SID1  6581" : "SID1  8580");
    g.text(412, kContentY + 2, kText, col::kSid2, s.sid.model[1] == sid::Model::k6581 ? "SID2  6581" : "SID2  8580");
    for (int ch = 0; ch < kChannels; ++ch) drawVoice(g, ch, ch < 3 ? 12 : 408, kContentY + 28 + (ch % 3) * 76);
    const int fy = kContentY + 28 + 3 * 76;
    char buf[64];
    for (int chip = 0; chip < kChips; ++chip) {
        const int x = chip ? 408 : 12;
        const bool sel = row_ == 6 + chip;
        g.fill(x, fy, 380, 44, sel ? col::kPanelHi : col::kPanel);
        const PerformOverrides& o = pl.perform;
        std::snprintf(buf, sizeof buf, "CUT %03X%+d", unsigned(pl.chipCutoff(chip)), int(o.cutoffOffset[chip]));
        g.text(x + 8, fy + 4, kMono, o.cutoffOffset[chip] ? col::kText : col::kDim, buf);
        if (o.resonance[chip] >= 0) std::snprintf(buf, sizeof buf, "RES %X (live)", unsigned(o.resonance[chip]));
        else std::snprintf(buf, sizeof buf, "RES %X", unsigned(pl.chipResonance(chip)));
        g.text(x + 200, fy + 4, kMono, o.resonance[chip] >= 0 ? col::kText : col::kDim, buf);
        if (sel) g.hline(x + 8 + ctl_ * 192, fy + 21, 96, col::kCursor);
        const uint16_t here = s.pattern(currentPattern()).autoLanes();
        if ((here >> (kAutoCutoff + chip)) & 1) g.text(x + 1, fy + 4, kMono, col::kEdit, "*");
        if ((here >> (kAutoRes + chip)) & 1) g.text(x + 193, fy + 4, kMono, col::kEdit, "*");
        meter(g, x + 8, fy + 28, 364, 8,
              float(pl.chipCutoff(chip) + o.cutoffOffset[chip] < 0 ? 0 : pl.chipCutoff(chip) + o.cutoffOffset[chip]) /
                  2047.0f,
              chip ? col::kSid2 : col::kSid1);
    }
    const int by = fy + 52;
    g.fill(12, by, 776, 26, row_ == 8 ? col::kPanelHi : col::kPanel);
    std::snprintf(buf, sizeof buf, "SID BALANCE  %u%%  (0 = mono, 100 = SID1 left / SID2 right)", unsigned(s.sid.balance));
    g.text(20, by + 5, kMono, col::kText, buf);
}

}  // namespace ui
}  // namespace sidt
