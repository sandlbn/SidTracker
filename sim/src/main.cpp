// SIDTracker desktop simulator: the portable app (src/app, src/ui, src/sid,
// src/tracker, src/audio) against the host HALs vendored from OpenTracker
// (sim/src, src/platform/PROVENANCE.md).
//
//   sidtracker-sim [--sd DIR] [--scale 1|2] [--no-audio] [--quit-after MS]
//   sidtracker-sim --script FILE [--screenshot OUT.png] [--panel] [--wav OUT.wav] [--sd DIR]
//
// Key map and script language: sim/README.md.
#include <SDL.h>

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app/app.h"
#include "canvas.h"
#include "host_hal.h"
#include "host_storage.h"
#include "panel.h"
#include "png.h"
#include "sdl_audio.h"
#include "sw_display.h"
#include "ui/buttons.h"

namespace b = ot::btn;
using namespace sim;

namespace {

struct Options {
    std::string sd;
    int scale = 1;
    bool noAudio = false, panelShot = false;
    std::string script, screenshot, wav;
    long quitAfterMs = -1;
};

void usage() {
    std::printf(
        "usage: sidtracker-sim [options]\n"
        "  --sd DIR          SD card root (default ~/.sidtracker-sd)\n"
        "  --scale N         window scale 1 or 2\n"
        "  --no-audio        do not open an audio device\n"
        "  --quit-after MS   exit after MS milliseconds (interactive)\n"
        "headless (no window, virtual time):\n"
        "  --script FILE     replay scripted input (sim/README.md)\n"
        "  --screenshot PNG  save the framebuffer at the end\n"
        "  --panel           screenshots include the whole front panel\n"
        "  --wav OUT.wav     record the rendered audio\n");
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--sd" && next(o.sd)) continue;
        if (a == "--scale" && next(v)) { o.scale = std::atoi(v.c_str()) >= 2 ? 2 : 1; continue; }
        if (a == "--no-audio") { o.noAudio = true; continue; }
        if (a == "--panel") { o.panelShot = true; continue; }
        if (a == "--script" && next(o.script)) continue;
        if (a == "--screenshot" && next(o.screenshot)) continue;
        if (a == "--wav" && next(o.wav)) continue;
        if (a == "--quit-after" && next(v)) { o.quitAfterMs = std::atol(v.c_str()); continue; }
        if (a == "-h" || a == "--help") { usage(); std::exit(0); }
        std::fprintf(stderr, "unknown or incomplete option: %s\n", a.c_str());
        usage();
        return false;
    }
    if (o.sd.empty()) {
        const char* home = std::getenv("HOME");
        o.sd = std::string(home ? home : ".") + "/.sidtracker-sd";
    }
    return true;
}

// Host time as 180 MHz cycles, so the benchmark page has numbers to show
// (they measure this PC, not the K66).
uint32_t hostCycles() {
    static const auto t0 = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    return uint32_t(uint64_t(ns) * 18u / 100u);
}

struct Machine {
    SimClock clock;
    SwDisplay display;
    SimInput input{clock};
    SimLeds leds;
    HostStorage storage;
    SdlAudio sdlAudio;
    ManualAudio manualAudio;
    std::unique_ptr<sidt::Pattern[]> patterns;
    std::vector<uint8_t> scratch = std::vector<uint8_t>(512 * 1024);   // GoatTracker files
    std::vector<int16_t> fxMemory = std::vector<int16_t>(2 * 88200);   // 2 s stereo delay
    std::vector<int16_t> kitMemory = std::vector<int16_t>(2 * 1024 * 1024);
    std::unique_ptr<sidt::App> app;
    static Machine* self;

    bool start(const Options& o, bool headless) {
        self = this;
        clock.setVirtual(headless);
        if (!storage.init(o.sd)) {
            std::fprintf(stderr, "cannot use SD folder %s\n", o.sd.c_str());
            return false;
        }
        ot::IAudioOut* audio = nullptr;
        if (headless) {
            audio = &manualAudio;
            if (!o.wav.empty() && !manualAudio.recordWav(o.wav.c_str()))
                std::fprintf(stderr, "cannot write %s\n", o.wav.c_str());
        } else if (!o.noAudio) {
            audio = &sdlAudio;
        }
        patterns.reset(new sidt::Pattern[sidt::kPatternCount]);
        sidt::Platform p;
        p.display = &display;
        p.audio = audio;
        p.storage = &storage;
        p.input = &input;
        p.leds = &leds;
        p.clock = &clock;
        p.sampleRateMilliHz = 44100000;   // the host device; the K66 runs 44,117,647
        p.patterns = patterns.get();
        p.scratch = scratch.data();
        p.scratchBytes = uint32_t(scratch.size());
        p.fxMemory = fxMemory.data();
        p.fxFrames = uint32_t(fxMemory.size() / 2);
        p.kitMemory = kitMemory.data();
        p.kitSamples = uint32_t(kitMemory.size());
        p.name = "simulator (host time)";
        p.cycleCounter = headless ? nullptr : &hostCycles;   // headless: deterministic
        app.reset(new sidt::App());
        if (!app->init(p)) {
            std::fprintf(stderr, "App::init failed\n");
            return false;
        }
        app->boot();
        return true;
    }
    void step() { app->loop(); }
    void shutdown() {
        app->engine.stop();
        if (app->dirty()) app->autosave();
        if (app->platform().audio) app->platform().audio->stop();
    }
};
Machine* Machine::self = nullptr;

void saveShot(Machine& m, const std::string& path, bool panel) {
    std::vector<uint32_t> fb(800 * 480);
    m.display.frontCopy(fb.data());
    bool ok;
    if (panel) {
        Canvas c(Panel::kW, Panel::kH);
        Panel().draw(c, fb.data(), m.input, m.leds, 0, "headless screenshot");
        ok = writePng(path.c_str(), c.pixels(), c.width(), c.height());
    } else {
        ok = writePng(path.c_str(), fb.data(), 800, 480);
    }
    std::fprintf(stderr, "%s %s\n", ok ? "saved" : "FAILED to save", path.c_str());
}

// Headless script runner: virtual time, one app loop pass per simulated
// millisecond, one 128-frame audio block every 128 / 44100 s.
struct Headless {
    Machine& m;
    bool panel;
    uint64_t frameAcc = 0;

    void advance(long ms) {
        for (long i = 0; i < ms; ++i) {
            m.clock.advanceUs(1000);
            frameAcc += uint64_t(ot::IAudioOut::kSampleRate);
            while (frameAcc >= uint64_t(ot::IAudioOut::kBlockFrames) * 1000) {
                frameAcc -= uint64_t(ot::IAudioOut::kBlockFrames) * 1000;
                m.manualAudio.pull();
            }
            m.step();
        }
    }

    bool button(const std::string& name, bool down, int line) {
        const int id = buttonByName(name.c_str());
        if (id < 0) {
            std::fprintf(stderr, "script:%d: unknown button '%s'\n", line, name.c_str());
            return false;
        }
        m.input.button(uint8_t(id), down);
        return true;
    }

    bool run(const std::string& path) {
        std::ifstream f(path);
        if (!f) {
            std::fprintf(stderr, "cannot read script %s\n", path.c_str());
            return false;
        }
        std::string lineStr;
        int line = 0;
        while (std::getline(f, lineStr)) {
            ++line;
            const size_t hash = lineStr.find('#');
            if (hash != std::string::npos) lineStr.resize(hash);
            std::istringstream ss(lineStr);
            std::string cmd, a1, a2, a3;
            ss >> cmd >> a1 >> a2 >> a3;
            if (cmd.empty()) continue;
            for (auto& ch : cmd) ch = char(std::tolower(uint8_t(ch)));
            const long n1 = std::atol(a1.c_str());
            if (cmd == "wait") advance(n1);
            else if (cmd == "press" || cmd == "down") { if (!button(a1, true, line)) return false; advance(1); }
            else if (cmd == "release" || cmd == "up") { if (!button(a1, false, line)) return false; advance(1); }
            else if (cmd == "tap" || cmd == "hold") {
                const long t = a2.empty() ? (cmd == "tap" ? 60 : 600) : std::atol(a2.c_str());
                if (!button(a1, true, line)) return false;
                advance(t);
                button(a1, false, line);
                advance(cmd == "tap" ? 60 : 1);
            } else if (cmd == "pad") {
                const int p = int(n1);
                const int vel = a3.empty() ? 100 : std::atoi(a3.c_str());
                if (a2 == "down") m.input.pad(uint8_t(p), true, uint8_t(vel));
                else if (a2 == "up") m.input.pad(uint8_t(p), false);
                else {
                    m.input.pad(uint8_t(p), true, uint8_t(vel));
                    advance(60);
                    m.input.pad(uint8_t(p), false);
                }
                advance(1);
            } else if (cmd == "enc") {
                m.input.encoder(int(n1));
                advance(1);
            } else if (cmd == "screenshot") {
                saveShot(m, a1, panel);
            } else if (cmd == "panelshot") {
                saveShot(m, a1, true);
            } else if (cmd == "echo") {
                std::fprintf(stderr, "script: %s\n", lineStr.c_str() + lineStr.find(a1));
            } else {
                std::fprintf(stderr, "script:%d: unknown command '%s'\n", line, cmd.c_str());
                return false;
            }
        }
        return true;
    }
};

int keyToButton(SDL_Scancode sc, bool padMode) {
    switch (sc) {
        case SDL_SCANCODE_F1: return b::F1;
        case SDL_SCANCODE_F2: return b::F2;
        case SDL_SCANCODE_F3: return b::F3;
        case SDL_SCANCODE_F4: return b::F4;
        case SDL_SCANCODE_F5: return b::F5;
        case SDL_SCANCODE_F6: return b::F6;
        case SDL_SCANCODE_F7: return b::F7;
        case SDL_SCANCODE_F8: return b::F8;
        case SDL_SCANCODE_F9: return b::Pattern;
        case SDL_SCANCODE_F10: return b::Song;
        case SDL_SCANCODE_F11: return b::Aux;
        case SDL_SCANCODE_F12: return b::Help;
        case SDL_SCANCODE_UP: return b::Up;
        case SDL_SCANCODE_DOWN: return b::Down;
        case SDL_SCANCODE_LEFT: return b::Left;
        case SDL_SCANCODE_RIGHT: return b::Right;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return b::Shift;
        case SDL_SCANCODE_SPACE: return b::Play;
        case SDL_SCANCODE_INSERT: return b::Insert;
        case SDL_SCANCODE_DELETE:
        case SDL_SCANCODE_BACKSPACE: return b::Delete;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return b::EncPush;
        default: break;
    }
    if (padMode) return -1;
    switch (sc) {
        case SDL_SCANCODE_R: return b::Rec;
        case SDL_SCANCODE_C: return b::Copy;
        case SDL_SCANCODE_N: return b::Note;
        case SDL_SCANCODE_I: return b::Instr;
        case SDL_SCANCODE_X: return b::Fx1;
        case SDL_SCANCODE_V: return b::Fx2;
        case SDL_SCANCODE_P: return b::Pattern;
        case SDL_SCANCODE_H: return b::Help;
        default: break;
    }
    // 1..0 -> module keys 0x08..0x11 (1 Instrument, 2 Perform, 3 File,
    // 4 Settings, 5 Benchmark, 0 Song).
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return 0x08 + (sc - SDL_SCANCODE_1);
    return -1;
}

int keyToPad(SDL_Scancode sc) {
    static const SDL_Scancode rows[4][12] = {
        {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
         SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS},
        {SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y,
         SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET,
         SDL_SCANCODE_RIGHTBRACKET},
        {SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
         SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE,
         SDL_SCANCODE_BACKSLASH},
        {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
         SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_UNKNOWN,
         SDL_SCANCODE_UNKNOWN},
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 12; ++c)
            if (rows[r][c] == sc && sc != SDL_SCANCODE_UNKNOWN) return r * 12 + c;
    return -1;
}

int runInteractive(Machine& m, const Options& o) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("SIDTracker simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       Panel::kW * o.scale, Panel::kH * o.scale, SDL_WINDOW_RESIZABLE);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(ren, Panel::kW, Panel::kH);
    SDL_Texture* tex =
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Panel::kW, Panel::kH);
    Panel panel;
    Canvas canvas(Panel::kW, Panel::kH);
    std::vector<uint32_t> fb(800 * 480);
    bool padMode = false, quit = false;
    uint64_t latched = 0;
    Control mouseCtl;
    uint32_t lastPresent = 0;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: quit = true; break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    if (ev.key.repeat) break;
                    const bool down = ev.type == SDL_KEYDOWN;
                    const SDL_Scancode sc = ev.key.keysym.scancode;
                    if (down && sc == SDL_SCANCODE_Q && (ev.key.keysym.mod & KMOD_CTRL)) { quit = true; break; }
                    if (sc == SDL_SCANCODE_TAB || sc == SDL_SCANCODE_CAPSLOCK) {
                        if (down) padMode = !padMode;
                        break;
                    }
                    if (sc == SDL_SCANCODE_PAGEUP || sc == SDL_SCANCODE_PAGEDOWN) {
                        if (down) m.input.encoder(sc == SDL_SCANCODE_PAGEUP ? 1 : -1);
                        break;
                    }
                    if (!padMode && (sc == SDL_SCANCODE_LEFTBRACKET || sc == SDL_SCANCODE_RIGHTBRACKET)) {
                        if (down) m.input.encoder(sc == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1);
                        break;
                    }
                    const int id = keyToButton(sc, padMode);
                    if (id >= 0) {
                        if (!((latched >> id) & 1)) m.input.button(uint8_t(id), down);
                        break;
                    }
                    if (padMode) {
                        const int p = keyToPad(sc);
                        if (p >= 0) m.input.pad(uint8_t(p), down, 100);
                    }
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    int d = ev.wheel.y;
                    if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) d = -d;
                    if (d) m.input.encoder(d);
                    break;
                }
                case SDL_MOUSEBUTTONDOWN: {
                    const Control c = panel.hit(ev.button.x, ev.button.y);
                    if (ev.button.button == SDL_BUTTON_RIGHT) {
                        if (c.kind == Control::kButton) {
                            latched ^= 1ull << c.id;
                            m.input.button(c.id, (latched >> c.id) & 1);
                        }
                        break;
                    }
                    if (ev.button.button != SDL_BUTTON_LEFT) break;
                    mouseCtl = c;
                    switch (c.kind) {
                        case Control::kButton:
                            if (!((latched >> c.id) & 1)) m.input.button(c.id, true);
                            break;
                        case Control::kPad: m.input.pad(c.id, true, 100); break;
                        case Control::kEncoderCcw: m.input.encoder(-1); break;
                        case Control::kEncoderCw: m.input.encoder(1); break;
                        case Control::kEncoderPush: m.input.button(b::EncPush, true); break;
                        default: break;
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP: {
                    if (ev.button.button != SDL_BUTTON_LEFT) break;
                    const Control c = mouseCtl;
                    mouseCtl = Control();
                    if (c.kind == Control::kButton && !((latched >> c.id) & 1)) m.input.button(c.id, false);
                    if (c.kind == Control::kPad) m.input.pad(c.id, false);
                    if (c.kind == Control::kEncoderPush) m.input.button(b::EncPush, false);
                    break;
                }
                default: break;
            }
        }
        m.step();
        const uint32_t now = SDL_GetTicks();
        if (o.quitAfterMs >= 0 && long(now) >= o.quitAfterMs) quit = true;
        if (now - lastPresent >= 16) {
            lastPresent = now;
            m.display.frontCopy(fb.data());
            const std::string status = std::string("Keys: ") +
                                       (padMode ? "PAD mode (letters/digits play pads)" : "COMMAND mode") +
                                       "\nTab toggles key mode; right-click latches a button\n"
                                       "1 Instr 2 Perform 3 File 4 Settings 5 Bench 0 Song F9 Pattern\nSD card: " +
                                       m.storage.root() + "\nAudio: 44100 Hz (device: 44117.6 Hz)\nCtrl+Q quits";
            panel.draw(canvas, fb.data(), m.input, m.leds, latched, status);
            SDL_UpdateTexture(tex, nullptr, canvas.pixels(), Panel::kW * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        } else {
            SDL_Delay(1);
        }
    }
    m.shutdown();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) return 2;
    const bool headless = !o.script.empty() || !o.screenshot.empty();
    Machine m;
    if (!m.start(o, headless)) return 1;
    if (!headless) return runInteractive(m, o);
    Headless h{m, o.panelShot};
    h.advance(300);
    const bool ok = o.script.empty() || h.run(o.script);
    h.advance(50);
    if (!o.screenshot.empty()) saveShot(m, o.screenshot, o.panelShot);
    m.shutdown();
    return ok ? 0 : 1;
}
