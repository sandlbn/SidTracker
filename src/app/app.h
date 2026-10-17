// SIDTracker -- application: song, audio engine, UI and project files over
// the HAL interfaces (src/platform/hal_api.h). Platform independent: the
// device glue (device_main.cpp) and the simulator (sim/src/main.cpp) fill a
// Platform and call run() / loop().
#pragma once

#include <cstdint>

#include "app/engine.h"
#include "compatibility/goat/export.h"
#include "compatibility/sid/sid_export.h"
#include "hal_api.h"
#include "tracker/song/song.h"
#include "ui/ui.h"

namespace sidt {

struct DriverStats {
    uint32_t minC = 0, avgC = 0, maxC = 0, blocks = 0;   // callback cycles (DWT) this window
    uint32_t underruns = 0;                              // cumulative late blocks
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
    void markDirty() { dirty_ = autoDirty_ = true; }
    bool dirty() const { return dirty_; }
    // Lists project names (sorted); returns the count (<= cap).
    int listProjects(char (*names)[kNameLen + 1], int cap);
    bool removeProject(const char* name);

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
    };
    const Settings& settings() const { return settings_; }
    void setVolume(uint8_t percent);
    void setTheme(uint8_t index);
    bool loadSettings();
    bool saveSettings();

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
    Settings settings_;
    bool settingsDirty_ = false;
    uint32_t settingsChanged_ = 0;
};

}  // namespace sidt
