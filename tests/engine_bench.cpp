// SIDTracker engine benchmark (PLAN.md §6; APPROACH.md §2 estimate).
//
//   make -C tests bench      host -O2: wall time per block
//   make -C tests bench-m4   Cortex-M4F code under QEMU mps2-an386 -icount:
//                            executed instructions per block
//
// Instruction counts are a floor for M4 cycles (no flash wait states, no
// FPU/load stalls); the K66 numbers come from the device benchmark page.
#include <cstdio>
#include <cstring>

#ifndef SIDT_BENCH_M4
#include <chrono>
#endif

#include "app/engine.h"

using namespace sidt;

namespace {

constexpr uint32_t kBudget = 522240;   // cycles per 128-frame block at 180 MHz

#ifdef SIDT_BENCH_M4
// CMSDK APB timer 0 at 25 MHz; with -icount shift=0 one instruction is 1 ns,
// so one timer tick = 40 instructions.
#define SIDT_BIG __attribute__((section(".psram")))
volatile uint32_t* const kTimer = reinterpret_cast<volatile uint32_t*>(0x40000000u);
void clockStart() {
    kTimer[0] = 0;
    kTimer[2] = 0xFFFFFFFFu;
    kTimer[1] = 0xFFFFFFFFu;
    kTimer[0] = 1;
}
uint32_t counter() { return (0xFFFFFFFFu - kTimer[1]) * 40u; }
const char* kUnit = "insn";
#else
#define SIDT_BIG
void clockStart() {}
uint32_t counter() {
    static const auto t0 = std::chrono::steady_clock::now();
    return uint32_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}
const char* kUnit = "ns";
#endif

SIDT_BIG Pattern gPatterns[kPatternCount];
Song gSong;
Engine gEngine;
int16_t gOut[256];

void report(const char* title, int blocks) {
    gEngine.takeProfile();
    for (int b = 0; b < blocks; ++b) gEngine.fill(gOut, 128);
    const Profile p = gEngine.takeProfile();
    std::printf("%s (%u blocks):\n", title, unsigned(p.blocks));
    for (int s = 0; s < Profile::kSections; ++s) {
        if ((s == Profile::kPcm || s == Profile::kEffects) && !p.peak[s]) continue;   // M8 parts unused
        std::printf("  %-12s avg %8u  peak %8u %s", Profile::name(s), unsigned(p.avg(s)), unsigned(p.peak[s]), kUnit);
#ifdef SIDT_BENCH_M4
        std::printf("  (%5.1f%% of %u)", 100.0 * p.avg(s) / kBudget, unsigned(kBudget));
#else
        std::printf("  (%5.2f%% of the 2902 us block)", 100.0 * p.avg(s) / 2902000.0);
#endif
        std::printf("\n");
    }
}

void songSixVoices() {
    gSong.clear();
    // Every channel plays the pulse lead (wave + pulse tables), SID1 and SID2
    // filters swept by a filter table, a new note every row.
    Instrument& in = gSong.inst[1];
    in.filterPtr = 1;
    Table& ft = gSong.tables[kFilterTable];
    ft.left[0] = 0x90; ft.right[0] = 0xC7;
    ft.left[1] = 0x00; ft.right[1] = 0x20;
    ft.left[2] = 0x40; ft.right[2] = 0x02;
    ft.left[3] = 0x40; ft.right[3] = 0xFE;
    ft.left[4] = 0xFF; ft.right[4] = 0x03;
    Pattern& p = gSong.pattern(0);
    for (int r = 0; r < p.rows; ++r)
        for (int ch = 0; ch < kChannels; ++ch) p.cells[r][ch] = {uint8_t(25 + ((r * 5 + ch * 7) % 36)), 1, 0, 0};
}

// M8 on top: all three effects, a drum on every row, both LFOs, voice pan.
void songExtended() {
    gSong.fx.delayMix = 40;
    gSong.fx.chorusMix = 40;
    gSong.fx.reverbMix = 40;
    gSong.mod.lfo[0] = Lfo{1, Lfo::kSine, 32, 40};
    gSong.mod.lfo[1] = Lfo{7, Lfo::kTri, 64, 40};
    for (int v = 0; v < 6; ++v) gSong.mod.pan[v] = int8_t(v * 40 - 100);
    Pattern& p = gSong.pattern(0);
    for (int r = 0; r < p.rows; ++r) p.drum[r] = uint8_t(1 + r % 8);
}

}  // namespace

constexpr uint32_t kFxFrames = 88200, kKitSamples = 512 * 1024;
SIDT_BIG int16_t gFx[2 * kFxFrames];
SIDT_BIG int16_t gKit[kKitSamples];

int main() {
    clockStart();
#ifdef SIDT_BENCH_M4
    std::printf("engine_bench (cortex-m4f -O2 under QEMU, instruction counts):\n");
#else
    std::printf("engine_bench (host -O2, wall time):\n");
#endif
    gSong.patterns = gPatterns;
    gSong.clear();
    gEngine.init(&gSong, 44117647);
    gEngine.setCycleCounter(&counter);
    report("idle (no notes)", 200);
    songSixVoices();
    gEngine.playSong(0);
    report("six voices, wave/pulse/filter tables, both filters", 1000);
    gEngine.setStress(true);
    gEngine.fill(gOut, 128);
    report("stress: blep saw/pulse + noise, all voices filtered, res F", 1000);
    gEngine.setStress(false);
    gEngine.setFxMemory(gFx, kFxFrames);
    gEngine.setKitMemory(gKit, kKitSamples);
    songExtended();
    gEngine.applySongConfig();
    gEngine.playSong(0);
    report("six voices + delay/chorus/reverb, drums every row, LFOs, pan", 1000);
    // Each effect alone (the effects row is the one to read).
    const char* names[3] = {"delay only", "chorus only", "reverb only"};
    for (int k = 0; k < 3; ++k) {
        gSong.fx.delayMix = k == 0 ? 40 : 0;
        gSong.fx.chorusMix = k == 1 ? 40 : 0;
        gSong.fx.reverbMix = k == 2 ? 40 : 0;
        report(names[k], 300);
    }
    return 0;
}
