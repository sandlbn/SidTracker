// SIDTracker -- application (app.h).
#include "app/app.h"

#include "ui/palette.h"

#include <cstdio>
#include <cctype>
#include <cstring>

#include "tracker/song/project_file.h"

namespace sidt {

namespace {

class FileWriter final : public project::Writer {
public:
    FileWriter(ot::IStorage& fs, ot::IStorage::Handle h) : fs_(fs), h_(h) {}
    bool write(const void* d, uint32_t n) override { return fs_.write(h_, d, n) == int32_t(n); }

private:
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
};

// IStorage reads may return short counts: loop until done or EOF.
class FileReader final : public project::Reader {
public:
    FileReader(ot::IStorage& fs, ot::IStorage::Handle h) : fs_(fs), h_(h) {}
    int32_t read(void* d, uint32_t n) override {
        uint32_t got = 0;
        while (got < n) {
            const int32_t r = fs_.read(h_, static_cast<uint8_t*>(d) + got, n - got);
            if (r < 0) return r;
            if (r == 0) break;
            got += uint32_t(r);
        }
        return int32_t(got);
    }

private:
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
};

bool endsWith(const char* s, const char* suffix) {
    const size_t a = std::strlen(s), b = std::strlen(suffix);
    if (a < b) return false;
    for (size_t i = 0; i < b; ++i) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != suffix[i]) return false;
    }
    return true;
}

}  // namespace

bool App::init(const Platform& p) {
    p_ = p;
    if (!p_.display || !p_.input || !p_.clock || !p_.patterns) return false;
    song.patterns = p_.patterns;
    song.clear();
    std::strcpy(fileName_, "UNTITLED");
    engine.init(&song, p_.sampleRateMilliHz);
    engine.setCycleCounter(p_.cycleCounter);
    engine.setMidiOut(p_.midi);
    engine.setFxMemory(p_.fxMemory, p_.fxFrames);
    engine.setKitMemory(p_.kitMemory, p_.kitSamples);
    if (p_.audio) {
        p_.audio->setCallback(&Engine::fillThunk, &engine);
        p_.audio->start();
    }
    if (p_.setRawDetents) p_.setRawDetents(true);   // jog wheel = value editor
    undo_.init(p_.undoMemory, p_.undoBytes, &song);
    ui.init(*this);
    lastAutosave_ = p_.clock->millis();
    return true;
}

void App::boot() {
    if (!p_.storage) return;
    ensureDirs();
    loadSettings();
    loadKit();
    if (p_.storage->exists(kAutosave) && loadPath(kAutosave)) {
        ui.message("Workspace restored");
    }
}

void App::loop() {
    if (p_.service) p_.service();
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    const uint32_t now = p_.clock->millis();
    ui.update(now);
    if (autoDirty_ && p_.storage && now - lastAutosave_ >= kAutosaveMs && !engine.player().playing()) autosave();
    if (settingsDirty_ && now - settingsChanged_ >= 1000) saveSettings();
    if (sidPlaying_) sidService();
    midiPoll();
    if (undoTouched_) {
        undoTouched_ = false;
        undo_.touched(now);
    }
    undo_.service(p_.clock->millis());
    // At once when the cursor reaches tracks 4-6 (their auditions must sound).
    if (now - lastCopyCheck_ >= 250 || (engine.sidCopy() && ui.channel >= 3 && !sidPlaying_)) {
        lastCopyCheck_ = now;
        updateSidCopy();
    }
}

void App::setSidCopy(bool on) {
    settings_.sidCopy = on ? 1 : 0;
    settingsDirty_ = true;
    settingsChanged_ = p_.clock->millis();
    updateSidCopy();
}

void App::updateSidCopy() {
    // SID 2 copies SID 1 when the setting is on and SID 2 has nothing of its
    // own: a one-SID .sid tune, or a song whose tracks 4-6 are empty.
    bool on = settings_.sidCopy != 0;
    if (on && sidPlaying_) {
        on = sidInfo_.sid2 == 0;
    } else if (on && ui.channel >= 3) {
        on = false;   // working on tracks 4-6: SID 2 is theirs
    } else if (on) {
        for (int i = 0; i < song.orderLen && on; ++i) {
            const Pattern& p = song.pattern(song.order[i].pattern);
            for (int r = 0; r < p.rows && on; ++r)
                for (int ch = 3; ch < kChannels; ++ch)
                    if (!p.cells[r][ch].empty()) on = false;
        }
    }
    engine.setSidCopy(on);
}

bool App::undo() { return undoSwap(false); }
bool App::redo() { return undoSwap(true); }

bool App::undoSwap(bool redo) {
    if (!undo_.ready()) return false;
    const uint32_t now = p_.clock->millis();
    if (undoTouched_) {
        undoTouched_ = false;
        undo_.touched(now);
    }
    undo_.service(now, true);   // an edit still pending is a step of its own
    const SidConfig sid = song.sid;
    engine.hold();
    const bool ok = redo ? undo_.redo() : undo_.undo();
    engine.release();
    if (!ok) return false;
    if (std::memcmp(&sid, &song.sid, sizeof sid)) engine.applySongConfig();
    dirty_ = autoDirty_ = true;
    ui.songEdited();
    return true;
}

void App::setMidi(uint8_t channel, uint8_t clockOut, uint8_t syncIn, uint8_t asidOut) {
    settings_.midiChannel = uint8_t(channel > 16 ? 0 : channel);
    settings_.clockOut = uint8_t(clockOut & 3);
    settings_.syncIn = uint8_t(syncIn > 2 ? 0 : syncIn);
    settings_.asidOut = uint8_t(asidOut & 3);
    engine.setClockPorts(settings_.clockOut);
    engine.setAsidPorts(settings_.asidOut);
    if (!settings_.syncIn) {
        sync_ = Sync{};
        engine.setExternalTickRate(0);
    }
    settingsDirty_ = true;
    settingsChanged_ = p_.clock->millis();
}

void App::midiPoll() {
    if (!p_.midi) return;
    uint8_t buf[64];
    const ot::MidiPortId ports[2] = {ot::MidiPortId::kDin, ot::MidiPortId::kUsb};
    for (int k = 0; k < 2; ++k) {
        for (int guard = 0; guard < 8; ++guard) {
            const uint32_t n = p_.midi->receive(ports[k], buf, sizeof buf);
            if (!n) break;
            midi::Parser::Msg m;
            for (uint32_t i = 0; i < n; ++i)
                if (midiIn_[k].feed(buf[i], m)) midiMessage(uint8_t(k + 1), m);
        }
    }
    midiSyncUpdate();
}

void App::midiMessage(uint8_t port, const midi::Parser::Msg& m) {
    if (m.status >= 0xF8) {   // real time: only from the sync port
        if (settings_.syncIn != port) return;
        switch (m.status) {
            case 0xF8: {
                const uint32_t now = p_.clock->micros();
                if (sync_.lastUs) {
                    const uint32_t d = now - sync_.lastUs;
                    if (d > 2000 && d < 250000)   // 10..1250 bpm
                        sync_.periodUs = sync_.periodUs ? (sync_.periodUs * 7 + d) / 8 : d;
                }
                sync_.lastUs = now;
                sync_.lastSeenMs = p_.clock->millis();
                if (sync_.running) {
                    // Each clock asks for speed / 6 ticks at the speed in force now
                    // (a tempo command or funk tempo changes it mid-song).
                    const uint8_t sp = engine.player().speed() ? engine.player().speed() : 6;
                    sync_.target += double(sp) / 6.0;
                    ++sync_.clocks;
                }
                break;
            }
            case 0xFA:   // start: the song from the top
            case 0xFB:   // continue: the song from the Song screen's position
                if (sidPlaying_) sidStop();
                if (m.status == 0xFA) engine.playSong(0);
                else engine.playSong(sync_.stopPos, sync_.stopRow);   // continue where Stop halted
                sync_.running = true;
                sync_.clocks = 0;
                sync_.target = 0;
                sync_.startTicks = engine.player().ticks();
                break;
            case 0xFC:
                sync_.stopPos = engine.player().orderPos();
                sync_.stopRow = engine.player().row();
                engine.stop();
                sync_.running = false;
                break;
            default: break;
        }
        return;
    }
    const uint8_t type = m.status & 0xF0, ch = uint8_t((m.status & 0x0F) + 1);
    if (settings_.midiChannel && ch != settings_.midiChannel) return;
    if (type == 0x90 && m.d2) {
        ui.midiNote(m.d1, true);
    } else if (type == 0x80 || type == 0x90) {
        ui.midiNote(m.d1, false);
    } else if (type == 0xB0) {
        // CC: live cutoff (74), resonance (71) and pulse width (70) on top of
        // the song, as on the Perform page; 123 all notes off.
        PerformOverrides& po = engine.playerMutable().perform;
        switch (m.d1) {
            case 74:
                for (int c = 0; c < kChips; ++c) po.cutoffOffset[c] = int16_t((int(m.d2) - 64) * 24);
                break;
            case 71:
                for (int c = 0; c < kChips; ++c) po.resonance[c] = int8_t(m.d2 >> 3);
                break;
            case 70:
                for (int v = 0; v < kChannels; ++v) po.pwOffset[v] = int16_t((int(m.d2) - 64) * 32);
                break;
            case 123:
                for (int v = 0; v < kChannels; ++v) engine.noteOff(v);
                break;
            default: break;
        }
    }
}

void App::midiSyncUpdate() {
    if (!settings_.syncIn) return;
    const uint32_t nowMs = p_.clock->millis();
    if (!sync_.periodUs || nowMs - sync_.lastSeenMs > 500) {   // no clock: own tempo
        if (sync_.locked) engine.setExternalTickRate(0);
        sync_.locked = false;
        sync_.bpmX10 = 0;
        return;
    }
    // Ticks a second = clocks a second x speed / 6, nudged so the song
    // position follows the clocks counted since Start.
    const uint32_t speed = engine.player().speed() ? engine.player().speed() : 6;
    double rate = 1e6 / double(sync_.periodUs) * double(speed) / 6.0;
    if (sync_.running && engine.player().playing()) {
        const double target = sync_.target;
        const double actual = double(engine.player().ticks() - sync_.startTicks);
        double c = (target - actual) * 0.02;
        c = c < -0.05 ? -0.05 : (c > 0.05 ? 0.05 : c);
        rate *= 1.0 + c;
    }
    engine.setExternalTickRate(uint32_t(rate * 1000.0));
    sync_.locked = true;
    sync_.bpmX10 = uint32_t(600000000.0 / (double(sync_.periodUs) * 24.0) * 10.0 / 10.0);
}

bool App::sidPlay(const char* path, int song) {
    sidStop();
    sidError_ = "";
    if (!p_.storage || !p_.c64Memory || !p_.scratch) {
        sidError_ = "not available on this platform";
        return false;
    }
    sidPlayer_.setMemory(p_.c64Memory, p_.c64Memory + c64::SidPlayer::kRamBytes);
    uint32_t n = 0;
    const uint32_t cap = 0x7C + 2 + 65536 + 1024;
    uint8_t* buf = static_cast<uint8_t*>(p_.scratch);
    if (p_.scratchBytes < cap || !readFile(path, buf, cap, n)) {
        sidError_ = "cannot read the file";
        return false;
    }
    const char* err = nullptr;
    if (!sidPlayer_.load(buf, n, sidInfo_, err)) {
        sidError_ = err ? err : "not a .sid file";
        return false;
    }
    c64::SidWrite initWrites[c64::SidPlayer::kMaxWrites];
    int nInit = 0;
    if (!sidPlayer_.start(song ? song : sidInfo_.start, initWrites, &nInit)) {
        sidError_ = "the tune's init routine crashed";
        return false;
    }
    std::snprintf(sidPath_, sizeof sidPath_, "%s", path);
    engine.stop();
    for (int c = 0; c < kChips; ++c) {
        const uint8_t m = sidInfo_.model[c];
        engine.setModel(c, m == 1 ? sid::Model::k6581 : sid::Model::k8580);
    }
    engine.setClock(sidInfo_.ntsc ? SidClock::kNtsc : SidClock::kPal);
    sidPlaying_ = true;
    updateSidCopy();   // before the tune's first frames: a two-SID tune keeps SID 2
    engine.startExternal(sidPlayer_.rateMilliHz());
    engine.pushFrame(initWrites, nInit);   // what init set up (volume, filter), first
    sidPlaying_ = true;
    sidFramesPlayed_ = 0;
    sidService();
    return true;
}

void App::sidStop() {
    if (!sidPlaying_) return;
    sidPlaying_ = false;
    engine.stopExternal();
    engine.applySongConfig();   // the song's SID models, clock, balance
    updateSidCopy();
}

void App::sidService() {
    // Keep about 8 frames (160 ms) queued: the main loop may be busy drawing.
    c64::SidWrite w[c64::SidPlayer::kMaxWrites];
    for (int guard = 0; guard < 16 && engine.framesQueued() < 8; ++guard) {
        int n = 0;
        if (!sidPlayer_.frame(w, n)) {
            sidError_ = "the tune crashed or hung (stopped)";
            sidStop();
            return;
        }
        engine.pushFrame(w, n);
        ++sidFramesPlayed_;
    }
}

uint32_t App::sidSeconds() const {
    const uint32_t rate = sidPlayer_.rateMilliHz();
    const uint32_t queued = engine.framesQueued();
    const uint32_t played = sidFramesPlayed_ > queued ? sidFramesPlayed_ - queued : 0;
    return rate ? uint32_t(uint64_t(played) * 1000u / rate) : 0;
}

bool App::screenshot(char* name, uint32_t cap) {
    constexpr uint32_t kW = 800, kH = 480, kBand = 16, kRowBytes = kW * 3;
    if (!p_.storage || !p_.display || !p_.scratch || p_.scratchBytes < kBand * kW * 4 + kBand * kRowBytes)
        return false;
    ensureDirs();
    p_.storage->mkdir(kShotDir);
    char path[64];
    int n = 1;
    for (; n < 10000; ++n) {
        std::snprintf(path, sizeof path, "%s/SHOT%04d.bmp", kShotDir, n);
        if (!p_.storage->exists(path)) break;
    }
    if (n >= 10000) return false;
    uint32_t* px = static_cast<uint32_t*>(p_.scratch);
    uint8_t* rows = reinterpret_cast<uint8_t*>(px + kBand * kW);
    // First band read before the file exists: a display without readback
    // leaves no empty file behind.
    if (!p_.display->readPixels(0, 0, kW, kBand, px)) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    // BITMAPFILEHEADER + BITMAPINFOHEADER, 24 bit, top-down (negative height).
    uint8_t hd[54] = {'B', 'M'};
    auto le32 = [&](int at, uint32_t v) {
        for (int i = 0; i < 4; ++i) hd[at + i] = uint8_t(v >> (8 * i));
    };
    le32(2, 54 + kRowBytes * kH);
    le32(10, 54);
    le32(14, 40);
    le32(18, kW);
    le32(22, uint32_t(-int32_t(kH)));
    hd[26] = 1;
    hd[28] = 24;
    le32(34, kRowBytes * kH);
    bool ok = p_.storage->write(h, hd, sizeof hd) == int32_t(sizeof hd);
    for (uint32_t y = 0; ok && y < kH; y += kBand) {
        if (y && !p_.display->readPixels(0, uint16_t(y), kW, kBand, px)) ok = false;
        for (uint32_t i = 0; ok && i < kBand * kW; ++i) {
            rows[3 * i] = uint8_t(px[i]);             // B
            rows[3 * i + 1] = uint8_t(px[i] >> 8);    // G
            rows[3 * i + 2] = uint8_t(px[i] >> 16);   // R
        }
        if (ok) ok = p_.storage->write(h, rows, kBand * kRowBytes) == int32_t(kBand * kRowBytes);
    }
    p_.storage->close(h);
    if (!ok) {
        p_.storage->remove(path);
        return false;
    }
    std::snprintf(name, cap, "Saved SHOT%04d.bmp", n);
    return true;
}

void App::setVolume(uint8_t percent) {
    settings_.volume = percent > 100 ? 100 : percent;
    engine.setMasterVolume(settings_.volume);
    settingsDirty_ = true;
    settingsChanged_ = p_.clock->millis();
}

void App::setTheme(uint8_t index) {
    ui::applyTheme(index);
    settings_.theme = uint8_t(ui::currentTheme());
    ui.requestRedraw();
    settingsDirty_ = true;
    settingsChanged_ = p_.clock->millis();
}

bool App::loadSettings() {
    char buf[256];
    uint32_t n = 0;
    if (!p_.storage || !p_.storage->exists(kSettingsFile) ||
        !readFile(kSettingsFile, reinterpret_cast<uint8_t*>(buf), sizeof buf - 1, n))
        return false;
    buf[n] = 0;
    for (char* line = buf; line && *line;) {
        char* next = std::strchr(line, '\n');
        if (next) *next++ = 0;
        unsigned v = 0;
        if (std::sscanf(line, "volume=%u", &v) == 1) settings_.volume = uint8_t(v > 100 ? 100 : v);
        else if (std::sscanf(line, "theme=%u", &v) == 1) settings_.theme = uint8_t(v);
        else if (std::sscanf(line, "midi_channel=%u", &v) == 1) settings_.midiChannel = uint8_t(v > 16 ? 0 : v);
        else if (std::sscanf(line, "midi_clock_out=%u", &v) == 1) settings_.clockOut = uint8_t(v & 3);
        else if (std::sscanf(line, "midi_sync_in=%u", &v) == 1) settings_.syncIn = uint8_t(v > 2 ? 0 : v);
        else if (std::sscanf(line, "asid_out=%u", &v) == 1) settings_.asidOut = uint8_t(v & 3);
        else if (std::sscanf(line, "sid_copy=%u", &v) == 1) settings_.sidCopy = uint8_t(v ? 1 : 0);
        line = next;
    }
    engine.setMasterVolume(settings_.volume);
    engine.setClockPorts(settings_.clockOut);
    engine.setAsidPorts(settings_.asidOut);
    ui::applyTheme(settings_.theme);
    settings_.theme = uint8_t(ui::currentTheme());
    ui.requestRedraw();
    return true;
}

bool App::saveSettings() {
    settingsDirty_ = false;
    if (!p_.storage) return false;
    ensureDirs();
    char buf[192];
    const int n = std::snprintf(buf, sizeof buf,
                                "volume=%u\ntheme=%u\nmidi_channel=%u\nmidi_clock_out=%u\nmidi_sync_in=%u\nasid_out=%u\n"
                                "sid_copy=%u\n",
                                unsigned(settings_.volume), unsigned(settings_.theme), unsigned(settings_.midiChannel),
                                unsigned(settings_.clockOut), unsigned(settings_.syncIn), unsigned(settings_.asidOut),
                                unsigned(settings_.sidCopy));
    const ot::IStorage::Handle h = p_.storage->open(kSettingsFile, ot::IStorage::kWrite);
    if (h < 0) return false;
    const bool ok = p_.storage->write(h, buf, uint32_t(n)) == n;
    p_.storage->close(h);
    return ok;
}

void App::run() {
    for (;;) loop();
}

void App::ensureDirs() {
    if (!p_.storage) return;
    p_.storage->mkdir(kDir);
    p_.storage->mkdir(kProjectDir);
}

void App::projectPath(const char* name, char* out, uint32_t cap) const {
    std::snprintf(out, cap, "%s/%s%s", kProjectDir, name, project::kExtension);
}

bool App::savePath(const char* path) {
    if (!p_.storage) return false;
    ensureDirs();
    // Write a temporary file, then replace: a failed write keeps the old one.
    char tmp[96];
    std::snprintf(tmp, sizeof tmp, "%s.tmp", path);
    const ot::IStorage::Handle h = p_.storage->open(tmp, ot::IStorage::kWrite);
    if (h < 0) return false;
    FileWriter w(*p_.storage, h);
    const bool ok = project::save(song, w) == project::Result::kOk;
    p_.storage->close(h);
    if (!ok) {
        p_.storage->remove(tmp);
        return false;
    }
    p_.storage->remove(path);
    return p_.storage->rename(tmp, path);
}

bool App::loadPath(const char* path) {
    if (!p_.storage) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    engine.stop();
    engine.hold();
    FileReader r(*p_.storage, h);
    const project::Result res = project::load(song, r);
    p_.storage->close(h);
    if (res != project::Result::kOk) {
        song.clear();   // never leave a half-loaded song
        char msg[64];
        std::snprintf(msg, sizeof msg, "Load failed: %s", project::resultText(res));
        ui.message(msg, 3000);
    }
    engine.panic();
    engine.applySongConfig();
    engine.release();
    undo_.reset();
    ui.songChanged();
    return res == project::Result::kOk;
}

bool App::save(const char* name) {
    char path[96];
    projectPath(name, path, sizeof path);
    if (!savePath(path)) {
        ui.message("Save failed", 3000);
        return false;
    }
    std::snprintf(fileName_, sizeof fileName_, "%s", name);
    dirty_ = false;
    autosave();
    return true;
}

bool App::load(const char* name) {
    char path[96];
    projectPath(name, path, sizeof path);
    if (!loadPath(path)) return false;
    std::snprintf(fileName_, sizeof fileName_, "%s", name);
    dirty_ = false;
    return true;
}

void App::newSong() {
    engine.stop();
    engine.hold();
    song.clear();
    engine.panic();
    engine.applySongConfig();
    engine.release();
    std::strcpy(fileName_, "UNTITLED");
    dirty_ = false;
    undo_.reset();
    ui.songChanged();
}

void App::autosave() {
    lastAutosave_ = p_.clock->millis();
    if (savePath(kAutosave)) autoDirty_ = false;
}

// --- GoatTracker files ---------------------------------------------------------

namespace {
class SinkFile final : public goat::Sink {
public:
    SinkFile(ot::IStorage& fs, ot::IStorage::Handle h) : fs_(fs), h_(h) {}
    bool put(const void* d, uint32_t n) override { return !n || fs_.write(h_, d, n) == int32_t(n); }

private:
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
};
}  // namespace

goat::Song* App::goatScratch() {
    if (!p_.scratch || p_.scratchBytes < sizeof(goat::Song) + kGoatFileMax) return nullptr;
    return static_cast<goat::Song*>(p_.scratch);
}

bool App::readFile(const char* path, uint8_t* dst, uint32_t cap, uint32_t& size) {
    size = 0;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    bool ok = true;
    for (;;) {
        if (size >= cap) {
            uint8_t extra;
            ok = p_.storage->read(h, &extra, 1) == 0;   // must be at the end
            break;
        }
        const int32_t r = p_.storage->read(h, dst + size, cap - size);
        if (r < 0) {
            ok = false;
            break;
        }
        if (r == 0) break;
        size += uint32_t(r);
    }
    p_.storage->close(h);
    return ok;
}

bool App::importGoat(const char* file) {
    goatReport_ = goat::Report{};
    goat::Song* gt = goatScratch();
    if (!p_.storage || !gt) {
        goatReport_.note("not available on this platform");
        return false;
    }
    // The raw file goes after the goat::Song in the scratch memory.
    uint8_t* raw = static_cast<uint8_t*>(p_.scratch) + ((sizeof(goat::Song) + 15) & ~size_t(15));
    const uint32_t rawCap = uint32_t(p_.scratchBytes - ((sizeof(goat::Song) + 15) & ~size_t(15)));
    char path[96];
    std::snprintf(path, sizeof path, "%s/%s", kGoatDir, file);
    uint32_t size = 0;
    if (!readFile(path, raw, rawCap, size)) {
        goatReport_.note("cannot read %s", file);
        return false;
    }
    if (endsWith(file, ".ins")) {
        goat::InsFile* ins = reinterpret_cast<goat::InsFile*>(gt);
        const goat::Result r = goat::readIns({raw, size}, *ins);
        if (r != goat::Result::kOk) {
            goatReport_.note("%s", goat::resultText(r));
            return false;
        }
        if (!goat::importInstrument(*ins, ui.inst, song, goatReport_)) return false;
        markDirty();
        ui.songEdited();   // the instrument screen re-reads the slot
        return true;
    }
    const goat::Result r = goat::readSong({raw, size}, *gt);
    if (r != goat::Result::kOk) {
        goatReport_.note("%s", goat::resultText(r));
        return false;
    }
    engine.stop();
    engine.hold();
    const bool ok = goat::importSong(*gt, 0, song, goatReport_);
    if (!ok) song.clear();
    engine.panic();
    engine.applySongConfig();
    engine.release();
    undo_.reset();
    ui.songChanged();
    if (ok) {
        // The project keeps the GT file's name (without extension).
        size_t n = std::strlen(file);
        if (n > 4) n -= 4;
        if (n > size_t(kNameLen)) n = size_t(kNameLen);
        std::memcpy(fileName_, file, n);
        fileName_[n] = 0;
        dirty_ = autoDirty_ = true;
    }
    return ok;
}

goat::ExportCheck App::checkGoat() { return goat::checkExport(song); }

psid::ExportCheck App::checkSid() {
    if (!p_.scratch || p_.scratchBytes < psid::kWorkBytes) {
        psid::ExportCheck c;
        c.ok = false;
        c.report.note("! not available on this platform");
        return c;
    }
    return psid::checkExport(song, static_cast<uint8_t*>(p_.scratch), p_.scratchBytes);
}

bool App::exportSidFile(const char* file) {
    goatReport_ = goat::Report{};
    if (!p_.storage || !p_.scratch || p_.scratchBytes < psid::kWorkBytes) {
        goatReport_.note("not available on this platform");
        return false;
    }
    uint8_t* buf = static_cast<uint8_t*>(p_.scratch);
    const uint32_t n = psid::exportSid(song, buf, p_.scratchBytes, goatReport_);
    if (!n) return false;
    ensureDirs();
    p_.storage->mkdir(kSidDir);
    char path[96];
    std::snprintf(path, sizeof path, "%s/%s", kSidDir, file);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) {
        goatReport_.note("cannot write %s", file);
        return false;
    }
    const bool ok = p_.storage->write(h, buf, n) == int32_t(n);
    p_.storage->close(h);
    if (!ok) p_.storage->remove(path);   // no truncated .sid in place of an older good one
    goatReport_.ok = ok;
    goatReport_.note("%s: %s (%u bytes)", ok ? "saved" : "write error", file, unsigned(n));
    return ok;
}

bool App::exportGoatSong(const char* file) {
    goatReport_ = goat::Report{};
    goat::Song* gt = goatScratch();
    if (!p_.storage || !gt) {
        goatReport_.note("not available on this platform");
        return false;
    }
    if (!goat::exportSong(song, *gt, goatReport_)) return false;
    ensureDirs();
    p_.storage->mkdir(kGoatDir);
    char path[96];
    std::snprintf(path, sizeof path, "%s/%s", kGoatDir, file);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) {
        goatReport_.note("cannot write %s", file);
        return false;
    }
    SinkFile out(*p_.storage, h);
    const bool ok = goat::writeSong(*gt, out);
    p_.storage->close(h);
    if (!ok) {
        p_.storage->remove(path);   // no truncated file in place of an older good one
        goatReport_.note("write error");
    }
    return ok;
}

bool App::exportGoatInstrument(int slot, const char* file) {
    goatReport_ = goat::Report{};
    goat::InsFile ins;
    if (!p_.storage || !goat::exportInstrument(song, slot, ins)) {
        goatReport_.note("not available");
        return false;
    }
    ensureDirs();
    p_.storage->mkdir(kGoatDir);
    char path[96];
    std::snprintf(path, sizeof path, "%s/%s", kGoatDir, file);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) {
        goatReport_.note("cannot write %s", file);
        return false;
    }
    SinkFile out(*p_.storage, h);
    const bool ok = goat::writeIns(ins, out);
    p_.storage->close(h);
    goatReport_.ok = ok;
    goatReport_.note("%s: %s", ok ? "saved" : "write error", file);
    return ok;
}

int App::listGoat(char (*names)[kNameLen + 5], int cap) {
    if (!p_.storage) return 0;
    struct Ctx {
        char (*names)[kNameLen + 5];
        int cap, n;
    } ctx{names, cap, 0};
    p_.storage->listDir(kGoatDir,
                        [](const ot::IStorage::DirEntry& e, void* c) {
                            Ctx& x = *static_cast<Ctx*>(c);
                            if (e.isDir || !(endsWith(e.name, ".sng") || endsWith(e.name, ".ins"))) return true;
                            if (std::strlen(e.name) > size_t(kNameLen + 4)) return true;
                            if (x.n >= x.cap) return false;
                            std::snprintf(x.names[x.n++], kNameLen + 5, "%s", e.name);
                            return true;
                        },
                        &ctx);
    for (int i = 1; i < ctx.n; ++i)
        for (int j = i; j > 0 && std::strcmp(names[j - 1], names[j]) > 0; --j) {
            char t[kNameLen + 5];
            std::memcpy(t, names[j], sizeof t);
            std::memcpy(names[j], names[j - 1], sizeof t);
            std::memcpy(names[j - 1], t, sizeof t);
        }
    return ctx.n;
}

const char* App::instrumentFile(int i) const {
    if (i < 0 || i >= instCount_ || !p_.listMemory) return "";
    return reinterpret_cast<const char*>(p_.listMemory + uint32_t(i) * kInstRecord);
}

int App::scanInstruments() {
    instCount_ = 0;
    if (!p_.storage || !p_.listMemory || p_.listBytes < kInstListBytes) return 0;
    struct Ctx {
        App* app;
        const char* folder;
        char (*sub)[40];
        int subs;
    };
    // Folders first (one level), then the files of the top folder and of each.
    char folders[32][40];
    Ctx ctx{this, "", folders, 0};
    auto add = [](const ot::IStorage::DirEntry& e, void* c) {
        Ctx& x = *static_cast<Ctx*>(c);
        if (e.isDir) {
            if (!x.folder[0] && x.subs < 32 && std::strlen(e.name) < 40) std::snprintf(x.sub[x.subs++], 40, "%s", e.name);
            return true;
        }
        if (!endsWith(e.name, ".ins") && !endsWith(e.name, ".INS")) return true;
        if (e.name[0] == '.') return true;   // macOS "._NAME.ins" metadata files
        if (x.app->instCount_ >= kInstMax) return false;
        char* rec = reinterpret_cast<char*>(x.app->p_.listMemory + uint32_t(x.app->instCount_) * kInstRecord);
        const int n = std::snprintf(rec, kInstRecord, "%s%s%s", x.folder, x.folder[0] ? "/" : "", e.name);
        if (n > 0 && n < kInstRecord) ++x.app->instCount_;
        return true;
    };
    p_.storage->listDir(kInstDir, add, &ctx);
    for (int f = 0; f < ctx.subs; ++f) {
        char path[96];
        std::snprintf(path, sizeof path, "%s/%s", kInstDir, folders[f]);
        ctx.folder = folders[f];
        p_.storage->listDir(path, add, &ctx);
    }
    // Sorted (case-insensitive), so folders group together.
    char* base = reinterpret_cast<char*>(p_.listMemory);
    auto less = [](const char* a, const char* b) {
        for (;; ++a, ++b) {
            const int x = std::toupper(static_cast<unsigned char>(*a)), y = std::toupper(static_cast<unsigned char>(*b));
            if (x != y || !x) return x < y;
        }
    };
    for (int i = 1; i < instCount_; ++i)
        for (int j = i; j > 0 && less(base + j * kInstRecord, base + (j - 1) * kInstRecord); --j) {
            char t[kInstRecord];
            std::memcpy(t, base + j * kInstRecord, kInstRecord);
            std::memcpy(base + j * kInstRecord, base + (j - 1) * kInstRecord, kInstRecord);
            std::memcpy(base + (j - 1) * kInstRecord, t, kInstRecord);
        }
    return instCount_;
}

bool App::loadInstrumentFile(int i, int slot) {
    goatReport_ = goat::Report{};
    goat::Song* gt = goatScratch();
    if (!p_.storage || !gt || i < 0 || i >= instCount_) return false;
    uint8_t* raw = static_cast<uint8_t*>(p_.scratch) + ((sizeof(goat::Song) + 15) & ~size_t(15));
    const uint32_t rawCap = uint32_t(p_.scratchBytes - ((sizeof(goat::Song) + 15) & ~size_t(15)));
    char path[128];
    std::snprintf(path, sizeof path, "%s/%s", kInstDir, instrumentFile(i));
    uint32_t size = 0;
    if (!readFile(path, raw, rawCap, size)) return false;
    goat::InsFile* ins = reinterpret_cast<goat::InsFile*>(gt);
    if (goat::readIns({raw, size}, *ins) != goat::Result::kOk) return false;
    return goat::importInstrument(*ins, slot, song, goatReport_);
}

bool App::saveInstrumentFile(int slot, const char* name) {
    goatReport_ = goat::Report{};
    goat::InsFile ins;
    if (!p_.storage || !goat::exportInstrument(song, slot, ins)) return false;
    ensureDirs();
    p_.storage->mkdir(kInstDir);
    char path[128];
    std::snprintf(path, sizeof path, "%s/User", kInstDir);
    p_.storage->mkdir(path);
    std::snprintf(path, sizeof path, "%s/User/%s.ins", kInstDir, name);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    SinkFile out(*p_.storage, h);
    const bool ok = goat::writeIns(ins, out);
    p_.storage->close(h);
    if (!ok) p_.storage->remove(path);
    return ok;
}

int App::loadKit() {
    if (!p_.storage || !p_.scratch || !p_.kitMemory) return 0;
    struct Ctx {
        char names[audio::kKitSlots][40];
        int slot[audio::kKitSlots];
        int n;
    };
    static Ctx ctx;
    ctx.n = 0;
    p_.storage->listDir(kKitDir,
                        [](const ot::IStorage::DirEntry& e, void* c) {
                            Ctx& x = *static_cast<Ctx*>(c);
                            const char* s = e.name;
                            if (e.isDir || !endsWith(s, ".wav") || x.n >= audio::kKitSlots) return true;
                            if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return true;
                            const int slot = (s[0] - '0') * 10 + (s[1] - '0');
                            if (slot < 1 || slot > audio::kKitSlots || std::strlen(s) >= 40) return true;
                            std::snprintf(x.names[x.n], 40, "%s", s);
                            x.slot[x.n++] = slot;
                            return true;
                        },
                        &ctx);
    engine.hold();
    engine.kit().synthesize();
    int loaded = 0;
    uint8_t* buf = static_cast<uint8_t*>(p_.scratch);
    for (int i = 0; i < ctx.n; ++i) {
        char path[96];
        std::snprintf(path, sizeof path, "%s/%s", kKitDir, ctx.names[i]);
        uint32_t size = 0;
        if (!readFile(path, buf, p_.scratchBytes, size)) continue;
        // Name: without the slot digits and the extension.
        char name[audio::kKitNameLen + 1];
        const char* base = ctx.names[i] + 2;
        if (*base == '_' || *base == ' ' || *base == '-') ++base;
        size_t len = std::strlen(base) - 4;
        if (len > size_t(audio::kKitNameLen)) len = audio::kKitNameLen;
        std::memcpy(name, base, len);
        name[len] = 0;
        for (char* c = name; *c; ++c)
            if (*c >= 'a' && *c <= 'z') *c = char(*c - 'a' + 'A');
        if (engine.kit().loadWav(ctx.slot[i], buf, size, name)) ++loaded;
    }
    engine.release();
    return loaded;
}

int App::listProjects(char (*names)[kNameLen + 1], int cap) {
    if (!p_.storage) return 0;
    struct Ctx {
        char (*names)[kNameLen + 1];
        int cap, n;
    } ctx{names, cap, 0};
    p_.storage->listDir(kProjectDir,
                        [](const ot::IStorage::DirEntry& e, void* c) {
                            Ctx& x = *static_cast<Ctx*>(c);
                            if (e.isDir || !endsWith(e.name, project::kExtension)) return true;
                            const size_t len = std::strlen(e.name) - (sizeof(project::kExtension) - 1);
                            if (len == 0 || len > size_t(kNameLen)) return true;
                            if (x.n >= x.cap) return false;
                            std::memcpy(x.names[x.n], e.name, len);
                            x.names[x.n][len] = 0;
                            ++x.n;
                            return true;
                        },
                        &ctx);
    // Insertion sort (small lists, no allocation).
    for (int i = 1; i < ctx.n; ++i)
        for (int j = i; j > 0 && std::strcmp(names[j - 1], names[j]) > 0; --j) {
            char t[kNameLen + 1];
            std::memcpy(t, names[j], sizeof t);
            std::memcpy(names[j], names[j - 1], sizeof t);
            std::memcpy(names[j - 1], t, sizeof t);
        }
    return ctx.n;
}

bool App::removeProject(const char* name) {
    if (!p_.storage) return false;
    char path[96];
    projectPath(name, path, sizeof path);
    return p_.storage->remove(path);
}

}  // namespace sidt
