// SIDTracker device entry point (MK66FX1M0). Boot order kept exactly as in
// OpenTracker (docs/firmware-map.md): USB diagnostics first, then rail + SD
// and the ot_restore.txt escape hatch before any other driver, then the
// rest of the board, then the app (only with working SDRAM: the patterns
// live there), else the hardware selftest.
#include <Arduino.h>

#include "app/app.h"
#include "audio_sgtl5000.h"
#include "board.h"
#include "diag.h"
#include "ft81x.h"
#include "fw_update.h"
#include "hal.h"
#include "input.h"
#include "sd_storage.h"
#include "selftest.h"

namespace {

using namespace ot;

// SDRAM layout (16 MB at 0x70000000, spec 01 §2): the pattern bank, then
// the GoatTracker import / export scratch.
constexpr uint32_t kPatternBytes = sizeof(sidt::Pattern) * sidt::kPatternCount;
constexpr uint32_t kScratchOffset = (kPatternBytes + 4095u) & ~4095u;
constexpr uint32_t kScratchBytes = 512u * 1024u;
static_assert(kScratchBytes >= sizeof(sidt::goat::Song) + sidt::App::kGoatFileMax, "GoatTracker scratch");
// Effects delay line (2 s stereo int16) and the PCM kit (4 M mono samples).
constexpr uint32_t kFxFrames = 88200;
constexpr uint32_t kFxOffset = (kScratchOffset + kScratchBytes + 4095u) & ~4095u;
constexpr uint32_t kKitOffset = (kFxOffset + kFxFrames * 4u + 4095u) & ~4095u;
constexpr uint32_t kKitSamples = 4u * 1024u * 1024u;
constexpr uint32_t kC64Offset = (kKitOffset + kKitSamples * 2u + 4095u) & ~4095u;   // SID player
constexpr uint32_t kUndoOffset = (kC64Offset + sidt::App::kC64MemoryBytes + 4095u) & ~4095u;   // undo history
constexpr uint32_t kListOffset = (kUndoOffset + sidt::Undo::kBytes + 4095u) & ~4095u;   // instrument library list
static_assert(kListOffset + sidt::App::kInstListBytes <= board::kSdramSize, "SDRAM layout exceeds 16 MB");

sidt::App gApp;

uint32_t dwtCycles() { return ARM_DWT_CYCCNT; }

bool driverStats(sidt::DriverStats& d) {
    hal::Sgtl5000Audio& a = hal::audio();
    d.blocks = a.renderCyclesWindow(d.minC, d.avgC, d.maxC);
    d.underruns = a.underruns();
    d.displayFaults = hal::display().faults();
    return true;
}

void setRawDetents(bool on) { hal::input().setRawDetents(on); }

// hal::powerOff() calls this with audio already stopped: keep the work.
void powerOffHook() { gApp.autosave(); }

}  // namespace

extern "C" int main(void) {
    hal::diagInit();
    if (hal::boardEarlyInit().sdMounted) {
        hal::diagStage("restore trigger check");
        hal::checkRestoreTrigger(hal::storage());
        hal::diagLog("restore: no trigger");
    }
    // Switched off with the power button (EEPROM power flag 0): stay off until
    // the button is pressed, as stock does, even when power comes back.
    hal::bootPowerGate();
    const hal::BootReport& boot = hal::boardInit();
#if !defined(OT_FORCE_SELFTEST)
    if (boot.sdramOk) {
        hal::diagStage("application");
        hal::diagFreezeBoot();
        hal::diagWatchdog(false);   // the app main loop has no diagBeat()
        // The codec init leaves the headphone amp muted (spec 01 §5.1).
        hal::audio().setHeadphoneVolume(0.7f);
        hal::HalSet& h = hal::halSet();
        sidt::Platform p;
        p.display = h.display;
        p.audio = h.audio;
        p.storage = boot.sdMounted ? h.storage : nullptr;
        p.input = h.input;
        p.leds = h.leds;
        p.clock = h.clock;
        p.service = h.service;
        p.sampleRateMilliHz = board::kSampleRateMilliHz;   // MCLK / 256 = 44,117,647 mHz
        p.patterns = reinterpret_cast<sidt::Pattern*>(board::kSdramBase);
        p.scratch = reinterpret_cast<uint8_t*>(board::kSdramBase) + kScratchOffset;
        p.scratchBytes = kScratchBytes;
        p.fxMemory = reinterpret_cast<int16_t*>(board::kSdramBase + kFxOffset);
        p.fxFrames = kFxFrames;
        p.kitMemory = reinterpret_cast<int16_t*>(board::kSdramBase + kKitOffset);
        p.kitSamples = kKitSamples;
        p.c64Memory = reinterpret_cast<uint8_t*>(board::kSdramBase + kC64Offset);
        p.undoMemory = reinterpret_cast<uint8_t*>(board::kSdramBase + kUndoOffset);
        p.undoBytes = sidt::Undo::kBytes;
        p.listMemory = reinterpret_cast<uint8_t*>(board::kSdramBase + kListOffset);
        p.listBytes = sidt::App::kInstListBytes;
        p.midi = h.midi;
        p.name = "K66 180 MHz";
        p.cycleCounter = &dwtCycles;
        p.driverStats = &driverStats;
        p.setRawDetents = &setRawDetents;
        p.isUpdateImageName = &hal::isUpdateImageName;
        p.requestFirmwareUpdate = &hal::requestFirmwareUpdate;
        if (gApp.init(p)) {
            hal::setPowerOffHook(&powerOffHook);
            gApp.boot();
            gApp.run();
        }
    }
#endif
    (void)boot;
    hal::diagStage("selftest");
    hal::diagWatchdog(true);
    hal::runSelftest();
}
