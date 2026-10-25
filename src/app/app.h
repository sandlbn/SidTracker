// SIDTracker -- application: song, audio engine, UI and project files over
// the HAL interfaces (src/platform/hal_api.h). Platform independent: the
// device glue (device_main.cpp) and the simulator (sim/src/main.cpp) fill a
// Platform and call run() / loop().
#pragma once

#include <cstdint>

#include "app/engine.h"
#include "app/undo.h"
#include "compatibility/goat/export.h"
#include "c64/sid_player.h"
#include "compatibility/sid/sid_export.h"
#include "hal_api.h"
#include "tracker/song/song.h"
#include "ui/ui.h"

namespace sidt {

struct DriverStats {
    uint32_t minC = 0, avgC = 0, maxC = 0, blocks = 0;   // callback cycles (DWT) this window
    uint32_t underruns = 0;                              // cumulative late blocks
    uint32_t displayFaults = 0;                          // FT81x coprocessor resets (lost frames)
};

struct Platform {
    ot::IDisplay* display = nullptr;
    ot::IAudioOut* audio = nullptr;
    ot::IStorage* storage = nullptr;   // null = no card
    ot::IInput* input = nullptr;
    ot::ILeds* leds = nullptr;
    ot::IClock* clock = nullptr;
    void (*service)() = nullptr;       // HAL housekeeping, once per loop pass
    uint32_t sampleRateMilliHz = 44100000;
    // Pattern memory for kPatternCount patterns (SDRAM on the device).
    Pattern* patterns = nullptr;
    // Scratch memory for GoatTracker import / export (>= sizeof(goat::Song),
    // about 135 KB; SDRAM on the device). Null = no GoatTracker files.
    void* scratch = nullptr;
    uint32_t scratchBytes = 0;
    // Effects delay line (interleaved stereo int16 frames) and PCM kit
    // memory (mono int16 samples); SDRAM on the device. Null = feature off.
    int16_t* fxMemory = nullptr;
    uint32_t fxFrames = 0;
    int16_t* kitMemory = nullptr;
    uint32_t kitSamples = 0;
    // SID player: C64 RAM (64 KB) + KERNAL stub (8 KB); SDRAM on the
    // device. Null = no SID player.
    uint8_t* c64Memory = nullptr;
    // Undo history (Undo::kBytes, about 2.2 MB; SDRAM on the device). Null
    // = no undo.
    uint8_t* undoMemory = nullptr;
    uint32_t undoBytes = 0;
    // Instrument library listing (App::kInstListBytes; SDRAM on the device).
    // Null = no card instruments in SOUNDS.
    uint8_t* listMemory = nullptr;
    uint32_t listBytes = 0;
    // MIDI DIN + USB-MIDI ports. Null = no MIDI.
    ot::IMidiPort* midi = nullptr;
    const char* name = "host";         // shown on the benchmark page
    // Optional device hooks (null in the simulator).
    uint32_t (*cycleCounter)() = nullptr;                                   // DWT CYCCNT
    bool (*driverStats)(DriverStats& out) = nullptr;                        // audio driver window
    void (*setRawDetents)(bool on) = nullptr;
    bool (*isUpdateImageName)(const char* name) = nullptr;
    bool (*requestFirmwareUpdate)(ot::IStorage& fs, const char* image) = nullptr;
};

class App {
public:
    static constexpr const char* kDir = "SIDTracker";
    static constexpr const char* kProjectDir = "SIDTracker/Projects";
    static constexpr const char* kAutosave = "SIDTracker/autosave.sidt";
    static constexpr uint32_t kAutosaveMs = 120000;

    bool init(const Platform& p);
    // Load the autosave if there is one (needs the card).
    void boot();
    void loop();                       // one main-loop pass
    [[noreturn]] void run();

    // Project files (main loop). Names are file names without directory
    // and extension, at most kNameLen characters.
    bool save(const char* name);
    bool load(const char* name);
    void newSong();
    void autosave();
    const char* fileName() const { return fileName_; }
    void markDirty() { dirty_ = autoDirty_ = undoTouched_ = true; }
    bool dirty() const { return dirty_; }
    // Lists project names (sorted); returns the count (<= cap).
    int listProjects(char (*names)[kNameLen + 1], int cap);
    bool removeProject(const char* name);

    // Undo / redo of song edits (Shift+Rec / Shift+Sample Rec). An edit
    // burst becomes one step once the song has been still for
    // Undo::kQuietMs. False: nothing to undo / redo.
    bool undo();
    bool redo();
    int undoSteps() const { return undo_.undoSteps(); }
    int redoSteps() const { return undo_.redoSteps(); }

    // GoatTracker files in kGoatDir (PLAN.md §16). `file` includes the
    // extension (.sng / .ins). The report of the last import / export /
    // check is kept in goatReport().
    static constexpr const char* kGoatDir = "SIDTracker/GoatTracker";
    // Largest .sng read (208 full patterns + order lists + tables).
    static constexpr uint32_t kGoatFileMax = 160 * 1024;
    bool importGoat(const char* file);              // .sng: replaces the song; .ins: into ui.inst
    goat::ExportCheck checkGoat();                  // what a song export keeps
    bool exportGoatSong(const char* file);
    bool exportGoatInstrument(int slot, const char* file);
    int listGoat(char (*names)[kNameLen + 5], int cap);

    // Instrument library on the card (UX plan P3.2): GoatTracker .ins files
    // in kInstDir and its folders (one level), listed in SOUNDS as CARD.
    static constexpr const char* kInstDir = "SIDTracker/Instruments";
    static constexpr int kInstRecord = 64;      // "Folder/NAME.ins"
    static constexpr int kInstMax = 512;
    static constexpr uint32_t kInstListBytes = uint32_t(kInstRecord) * kInstMax;
    int scanInstruments();                      // re-reads the card; returns the count
    int instrumentCount() const { return instCount_; }
    const char* instrumentFile(int i) const;    // relative to kInstDir; "" when out of range
    bool loadInstrumentFile(int i, int slot);   // into `slot` (tables appended)
    bool saveInstrumentFile(int slot, const char* name);   // kInstDir/User/<name>.ins

    // .sid export (M9) into kSidDir, `file` with the extension; the report
    // goes to goatReport(). Uses the scratch memory.
    static constexpr const char* kSidDir = "SIDTracker/SID";
    psid::ExportCheck checkSid();
    bool exportSidFile(const char* file);

    // PCM kit (M8): the built-in kit, then /SIDTracker/Kit/NNname.wav files
    // into slot NN (01..16). Returns the number of WAVs loaded.
    static constexpr const char* kKitDir = "SIDTracker/Kit";
    int loadKit();
    const goat::Report& goatReport() const { return goatReport_; }

    // Unit settings, not part of a song (Config: MASTER VOLUME, THEME), in
    // kSettingsFile as "key=value" lines; written 1 s after the last change.
    static constexpr const char* kSettingsFile = "SIDTracker/settings.txt";
    struct Settings {
        uint8_t volume = 80;   // percent
        uint8_t theme = 0;     // ui/palette.h
        // MIDI (Config). Ports: 0 off, 1 DIN, 2 USB, 3 both.
        uint8_t midiChannel = 0;   // notes / CC in: 0 = any channel, 1..16
        uint8_t clockOut = 0;      // MIDI clock + Start / Stop out
        uint8_t syncIn = 0;        // follow MIDI clock in: 0 off, 1 DIN, 2 USB
        uint8_t asidOut = 0;       // SID register frames out (ASID)
        uint8_t sidCopy = 0;       // 1: a one-SID song / tune also plays on SID 2 (both sides)
    };
    const Settings& settings() const { return settings_; }
    void setVolume(uint8_t percent);
    void setTheme(uint8_t index);
    void setMidi(uint8_t channel, uint8_t clockOut, uint8_t syncIn, uint8_t asidOut);
    void setSidCopy(bool on);
    // True when SID 2 is copying SID 1 right now (setting on and SID 2 unused).
    bool sidCopyActive() const { return engine.sidCopy(); }
    // MIDI clock in: true while an external clock drives the tempo.
    bool midiSynced() const { return sync_.locked; }
    uint32_t midiSyncBpmX10() const { return sync_.bpmX10; }
    bool loadSettings();
    bool saveSettings();

    // SID player (.sid files anywhere on the card; ui SID PLAYER screen).
    // `path` is relative to the card root; song 0 = the file's start song.
    // The tune runs in the main loop (sidService, from loop()) a few frames
    // ahead of the audio. sidError() says why play failed or stopped.
    static constexpr uint32_t kC64MemoryBytes = c64::SidPlayer::kRamBytes + c64::SidPlayer::kRomBytes;
    bool sidPlay(const char* path, int song = 0);
    void sidStop();
    bool sidPlaying() const { return sidPlaying_; }
    const c64::SidInfo& sidInfo() const { return sidInfo_; }
    int sidSong() const { return sidPlayer_.song(); }
    uint32_t sidSeconds() const;
    const char* sidError() const { return sidError_; }
    const char* sidPath() const { return sidPath_; }
    // Keeps a playing tune fed during long main-loop work (folder listings).
    void sidPump() {
        if (sidPlaying_) sidService();
    }

    // The frame on the display as a 24-bit BMP in kShotDir (SHOT0001.bmp,
    // ...; Shift + Power). `name` gets "Saved SHOTnnnn.bmp". Uses the scratch
    // memory; false when the display cannot read back or the card fails.
    static constexpr const char* kShotDir = "SIDTracker/Screenshots";
    bool screenshot(char* name, uint32_t cap);

    const Platform& platform() const { return p_; }
    Song song;
    Engine engine;
    ui::Ui ui;

private:
    void ensureDirs();
    void projectPath(const char* name, char* out, uint32_t cap) const;
    bool loadPath(const char* path);
    bool savePath(const char* path);

    bool readFile(const char* path, uint8_t* dst, uint32_t cap, uint32_t& size);
    goat::Song* goatScratch();

    Platform p_;
    goat::Report goatReport_;
    char fileName_[kNameLen + 1] = {};
    bool dirty_ = false;       // changed since the last save under fileName_
    bool autoDirty_ = false;   // changed since the last autosave
    uint32_t lastAutosave_ = 0;
    uint32_t lastCopyCheck_ = 0;
    void updateSidCopy();
    Undo undo_;
    int instCount_ = 0;
    bool undoTouched_ = false;   // markDirty since the last loop pass
    bool undoSwap(bool redo);
    void sidService();
    void midiPoll();
    void midiMessage(uint8_t port, const midi::Parser::Msg& m);
    void midiSyncUpdate();
    midi::Parser midiIn_[2];
    struct Sync {
        uint32_t lastUs = 0, periodUs = 0, lastSeenMs = 0;
        uint32_t clocks = 0, startTicks = 0;
        double target = 0;                  // ticks the clocks since Start ask for
        uint8_t stopPos = 0, stopRow = 0;   // where Stop halted (for Continue)
        uint32_t bpmX10 = 0;
        bool running = false, locked = false;
    } sync_;
    c64::SidPlayer sidPlayer_;
    c64::SidInfo sidInfo_;
    bool sidPlaying_ = false;
    const char* sidError_ = "";
    char sidPath_[160] = {};
    uint32_t sidFramesPlayed_ = 0;
    Settings settings_;
    bool settingsDirty_ = false;
    uint32_t settingsChanged_ = 0;
};

}  // namespace sidt
