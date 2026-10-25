// SIDTracker -- benchmark page (bench_screen.h).
#include "ui/settings/bench_screen.h"

#include <cstdio>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace {
constexpr uint32_t kCoreHz = 180000000;
}

void BenchScreen::poll(uint32_t now) {
    if (now - lastPoll_ < 1000) return;
    lastPoll_ = now;
    App& app = ui().app();
    window_ = app.engine.takeProfile();
    for (int s = 0; s < Profile::kSections; ++s)
        if (window_.peak[s] > peak_[s]) peak_[s] = window_.peak[s];
    DriverStats d;
    if (app.platform().driverStats && app.platform().driverStats(d)) {
        haveDriver_ = true;
        if (d.blocks) drvAvg_ = d.avgC;
        if (d.maxC > drvMax_) drvMax_ = d.maxC;
        underruns_ = d.underruns;
        displayFaults_ = d.displayFaults;
    }
}

uint32_t BenchScreen::cpuPermille(bool peak) const {
    const Engine& e = const_cast<BenchScreen*>(this)->ui().app().engine;
    const uint32_t budget = uint32_t(uint64_t(Engine::kBlock) * kCoreHz * 1000u / e.sampleRateMilliHz());
    const uint32_t c = peak ? peak_[Profile::kTotal] : window_.avg(Profile::kTotal);
    return budget ? uint32_t(uint64_t(c) * 1000u / budget) : 0;
}

bool BenchScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Engine& e = ui().app().engine;
    if (id == b::F1) {
        e.setStress(!e.stress());
        ui().status(e.stress() ? "Stress load on (player paused)" : "Stress load off");
        return true;
    }
    if (id == b::F2) {
        for (uint32_t& p : peak_) p = 0;
        drvMax_ = 0;
        return true;
    }
    return false;
}

void BenchScreen::labels(const char* out[8]) {
    out[0] = ui().app().engine.stress() ? "STRESS ON" : "STRESS";
    out[1] = "RESET PK";
}

void BenchScreen::draw(ot::Gfx& g) {
    App& app = ui().app();
    const Platform& p = app.platform();
    const uint32_t fsMilli = app.engine.sampleRateMilliHz();
    // Budget = block period x core clock (522,240 cycles at 44117.6 Hz).
    const uint32_t budget = uint32_t(uint64_t(Engine::kBlock) * kCoreHz * 1000u / fsMilli);
    const uint32_t periodUs = uint32_t(uint64_t(Engine::kBlock) * 1000000000u / fsMilli);
    const uint32_t total = window_.avg(Profile::kTotal);
    const uint32_t peak = peak_[Profile::kTotal];
    char buf[96];
    int y = kContentY + 4;
    auto line = [&](const char* label, const char* value, uint32_t c = col::kText) {
        g.text(16, y, kMono, col::kDim, label);
        g.text(220, y, kMono, c, value);
        y += 18;
    };
    std::snprintf(buf, sizeof buf, "%s, %u.%03u Hz", p.name, unsigned(fsMilli / 1000), unsigned(fsMilli % 1000));
    line("Platform:", buf);
    std::snprintf(buf, sizeof buf, "%u.%03u ms (128 frames)", unsigned(periodUs / 1000), unsigned(periodUs % 1000));
    line("Audio buffer period:", buf);
    std::snprintf(buf, sizeof buf, "%u cycles @ 180 MHz", unsigned(budget));
    line("Cycles available:", buf);
    if (!p.cycleCounter) {
        line("Cycles consumed:", "no cycle counter on this platform", col::kDim);
    } else {
        std::snprintf(buf, sizeof buf, "%u avg (engine)", unsigned(total));
        line("Cycles consumed:", buf);
        std::snprintf(buf, sizeof buf, "%u", unsigned(peak));
        line("Peak cycles:", buf, peak * 10u >= budget * 7u ? col::kBad : col::kText);
        std::snprintf(buf, sizeof buf, "%u.%u%% avg, %u.%u%% peak (alarm 70%%)", unsigned(total * 100u / budget),
                      unsigned(total * 1000u / budget % 10), unsigned(peak * 100u / budget),
                      unsigned(peak * 1000u / budget % 10));
        line("CPU utilization:", buf, peak * 10u >= budget * 7u ? col::kBad : col::kGood);
    }
    if (haveDriver_) {
        std::snprintf(buf, sizeof buf, "%u", unsigned(underruns_));
        line("Audio underruns:", buf, underruns_ ? col::kBad : col::kGood);
        std::snprintf(buf, sizeof buf, "%u (each one a lost frame: report which screen)", unsigned(displayFaults_));
        line("Display faults:", buf, displayFaults_ ? col::kBad : col::kGood);
        std::snprintf(buf, sizeof buf, "%u avg, %u peak cycles (whole ISR callback)", unsigned(drvAvg_), unsigned(drvMax_));
        line("Driver callback:", buf);
    } else {
        line("Audio underruns:", "not measured on this platform", col::kDim);
    }
    std::snprintf(buf, sizeof buf, "%u peak |sample|, %u clipped frames", unsigned(app.engine.peak()),
                  unsigned(app.engine.clips()));
    line("Output:", buf);

    // Per-section table.
    y += 6;
    g.text(16, y, kMono, col::kDim, "SECTION        AVG      PEAK    AVG%");
    y += 18;
    for (int s = 0; s < Profile::kSections; ++s) {
        const uint32_t a = window_.avg(s);
        std::snprintf(buf, sizeof buf, "%-12s %8u  %8u  %3u.%u", Profile::name(s), unsigned(a), unsigned(peak_[s]),
                      unsigned(a * 100u / budget), unsigned(a * 1000u / budget % 10));
        g.text(16, y, kMono, s == Profile::kTotal ? col::kText : col::kDim, buf);
        y += 16;
    }

    if (app.engine.stress()) g.text(520, kContentY + 4, kMono, col::kWarn, "STRESS LOAD RUNNING");
}

}  // namespace ui
}  // namespace sidt
