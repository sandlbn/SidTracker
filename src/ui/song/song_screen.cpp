// SIDTracker -- Song screen (song_screen.h).
#include "ui/song/song_screen.h"

#include <cstdio>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr int kVisible = 16;
}  // namespace

void SongScreen::encoder(int d) {
    Song& s = ui().song();
    OrderEntry& e = s.order[ui().orderPos];
    const bool sh = ui().shift;
    switch (col_) {
        case 0: e.pattern = uint8_t(clampi(e.pattern + d * (sh ? 16 : 1), 0, kPatternCount - 1)); break;
        case 1: e.transpose = int8_t(clampi(e.transpose + d * (sh ? 12 : 1), -48, 48)); break;
        default: e.repeat = uint8_t(clampi(e.repeat + d, 1, 16)); break;
    }
    ui().app().markDirty();
}

bool SongScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    Song& s = u.song();
    const int pos = u.orderPos;
    switch (id) {
        case b::Up: u.orderPos = uint8_t(clampi(pos - (u.shift ? kVisible : 1), 0, s.orderLen - 1)); return true;
        case b::Down: u.orderPos = uint8_t(clampi(pos + (u.shift ? kVisible : 1), 0, s.orderLen - 1)); return true;
        case b::Left: col_ = (col_ + 2) % 3; return true;
        case b::Right: col_ = (col_ + 1) % 3; return true;
        case b::Insert:
            if (s.orderLen >= kOrderLen - 1) return true;
            for (int i = s.orderLen; i > pos + 1; --i) s.order[i] = s.order[i - 1];
            s.order[pos + 1] = s.order[pos];
            ++s.orderLen;
            u.orderPos = uint8_t(pos + 1);
            u.app().markDirty();
            return true;
        case b::Delete:
            if (s.orderLen <= 1) return true;
            for (int i = pos; i + 1 < s.orderLen; ++i) s.order[i] = s.order[i + 1];
            --s.orderLen;
            if (u.orderPos >= s.orderLen) u.orderPos = uint8_t(s.orderLen - 1);
            if (s.restartPos >= s.orderLen) s.restartPos = 0;
            u.app().markDirty();
            return true;
        case b::F7:
        case b::EncPush:
            u.pattern = s.order[pos].pattern;
            u.show(ScreenId::kPattern);
            return true;
        case b::Play:
            if (u.app().engine.player().playing()) return false;   // Ui stops
            u.app().engine.playSong(uint8_t(pos));
            return true;
        case b::F1: s.restartPos = uint8_t(pos); u.app().markDirty(); return true;
        case b::F2: s.bpm = uint8_t(clampi(s.bpm - (u.shift ? 10 : 1), 32, 255)); u.app().markDirty(); return true;
        case b::F3: s.bpm = uint8_t(clampi(s.bpm + (u.shift ? 10 : 1), 32, 255)); u.app().markDirty(); return true;
        case b::F4: s.speed = uint8_t(clampi(s.speed - 1, 1, 31)); u.app().markDirty(); return true;
        case b::F5: s.speed = uint8_t(clampi(s.speed + 1, 1, 31)); u.app().markDirty(); return true;
        case b::F6:
            s.mode = s.mode == SongMode::kAuthentic ? SongMode::kExtended : SongMode::kAuthentic;
            u.app().markDirty();
            return true;
        default: return false;
    }
}

void SongScreen::labels(const char* out[8]) {
    out[0] = "RESTART";
    out[1] = "BPM -";
    out[2] = "BPM +";
    out[3] = "SPD -";
    out[4] = "SPD +";
    out[5] = ui().song().mode == SongMode::kAuthentic ? "AUTHENTIC" : "EXTENDED";
    out[6] = "EDIT PAT";
}

void SongScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    const Song& s = u.song();
    const Player& pl = u.app().engine.player();
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s  by %s", s.name, s.author[0] ? s.author : "-");
    g.text(16, kContentY + 2, kLarge, col::kText, buf);
    std::snprintf(buf, sizeof buf, "LENGTH %02X  RESTART %02X  BPM %u  SPEED %u  (%u Hz ticks)  %s", unsigned(s.orderLen),
                  unsigned(s.restartPos), unsigned(s.bpm), unsigned(s.speed), unsigned(s.bpm * 2 / 5),
                  s.mode == SongMode::kAuthentic ? "AUTHENTIC" : "EXTENDED");
    g.text(16, kContentY + 34, kMono, col::kDim, buf);
    const uint32_t ext = s.extendedFeatures();
    if (ext & kExtTempo) g.text(16, kContentY + 52, kMono, col::kWarn, "! tempo is not 50/60 Hz: GoatTracker export will change it");

    const int x0 = 60, y0 = kContentY + 76;
    g.text(x0, y0, kMono, col::kDim, "POS   PATTERN   TRANSPOSE   REPEAT");
    int top = u.orderPos - kVisible / 2;
    top = clampi(top, 0, s.orderLen > kVisible ? s.orderLen - kVisible : 0);
    const int colX[3] = {x0 + 56, x0 + 136, x0 + 232};
    for (int i = 0; i < kVisible; ++i) {
        const int pos = top + i;
        if (pos >= s.orderLen) break;
        const int y = y0 + 20 + i * 18;
        const OrderEntry& e = s.order[pos];
        if (pos == u.orderPos) g.fill(x0 - 8, y - 1, 330, 18, col::kPanelHi);
        if (pl.playing() && pl.mode() == Player::Mode::kSong && pl.orderPos() == pos) g.fill(x0 - 16, y - 1, 6, 18, col::kGood);
        std::snprintf(buf, sizeof buf, "%02X", unsigned(pos));
        g.text(x0, y, kMono, pos == s.restartPos ? col::kWarn : col::kDim, buf);
        std::snprintf(buf, sizeof buf, "%02X", unsigned(e.pattern));
        g.text(colX[0], y, kMono, col::kText, buf);
        std::snprintf(buf, sizeof buf, "%+d", int(e.transpose));
        g.text(colX[1], y, kMono, e.transpose ? col::kText : col::kDim, buf);
        std::snprintf(buf, sizeof buf, "%u", unsigned(e.repeat));
        g.text(colX[2], y, kMono, e.repeat > 1 ? col::kText : col::kDim, buf);
        if (pos == u.orderPos) g.hline(colX[col_], y + 15, col_ == 1 ? 32 : 16, col::kCursor);
    }
    g.text(440, y0, kMono, col::kDim, "restart position in yellow");
    g.text(440, y0 + 18, kMono, col::kDim, "F7 EDIT PAT: open the pattern");
    g.text(440, y0 + 36, kMono, col::kDim, "Insert: duplicate  Delete: remove");
    g.text(440, y0 + 54, kMono, col::kDim, "Play: from the cursor");
}

}  // namespace ui
}  // namespace sidt
