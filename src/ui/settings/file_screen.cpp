// SIDTracker -- File screen (file_screen.h).
#include "ui/settings/file_screen.h"

#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "ui/buttons.h"

namespace sidt {
namespace ui {

namespace b = ot::btn;

void FileScreen::enter() {
    pending_ = 0;
    refresh();
}

void FileScreen::refresh() {
    App& app = ui().app();
    if (goat_) {
        count_ = app.listGoat(names_, kMax);
    } else {
        char tmp[kMax][kNameLen + 1];
        count_ = app.listProjects(tmp, kMax);
        for (int i = 0; i < count_; ++i) {
            std::memcpy(names_[i], tmp[i], kNameLen + 1);
            names_[i][kNameLen] = 0;
        }
    }
    if (sel_ >= count_) sel_ = count_ ? count_ - 1 : 0;
}

bool FileScreen::confirm(uint8_t action, const char* question) {
    if (pending_ == action + 1) {
        pending_ = 0;
        return true;
    }
    pending_ = uint8_t(action + 1);
    ui().message(question, 3000);
    return false;
}

void FileScreen::encoder(int d) {
    if (!count_) return;
    sel_ = ((sel_ + d) % count_ + count_) % count_;
    pending_ = 0;
}

bool FileScreen::button(uint8_t id, bool down) {
    if (!down) return false;
    App& app = ui().app();
    if (!app.platform().storage) {
        if (id <= b::F8) ui().message("No SD card");
        return id <= b::F8;
    }
    switch (id) {
        case b::Up: encoder(-1); return true;
        case b::Down: encoder(1); return true;
        case b::F5:
            goat_ = !goat_;
            sel_ = 0;
            pending_ = 0;
            reportShown_ = false;
            refresh();
            return true;
        default: break;
    }
    return goat_ ? goatButton(id) : projectButton(id);
}

bool FileScreen::projectButton(uint8_t id) {
    App& app = ui().app();
    switch (id) {
        case b::F6: {
            if (pending_ != b::F6 + 1) {
                // First press: what plays on a C64 (M9).
                const psid::ExportCheck c = app.checkSid();
                reportShown_ = false;
                exportCheck_ = c.report;
                pending_ = b::F6 + 1;
                ui().message(c.ok ? "Press EXPORT SID again to write" : "Cannot export", 3000);
                if (!c.ok) {
                    pending_ = 0;
                    reportShown_ = true;
                    sidFailed_ = true;   // the check's report stays in exportCheck_
                }
                return true;
            }
            pending_ = 0;
            char file[kNameCap + 8];
            std::snprintf(file, sizeof file, "%s.sid", app.fileName());
            const bool ok = app.exportSidFile(file);
            reportShown_ = true;
            sidFailed_ = false;
            ui().message(ok ? "Exported to /SIDTracker/SID" : "Export failed", 2500);
            return true;
        }
        case b::F1:
        case b::EncPush:
            if (!count_) return true;
            if (app.dirty() && !confirm(b::F1, "Unsaved changes: press LOAD again")) return true;
            if (app.load(names_[sel_])) ui().message("Loaded");
            return true;
        case b::F2: {
            const char* name = app.fileName();
            if (!std::strcmp(name, "UNTITLED")) return projectButton(b::F3);
            if (app.save(name)) ui().message("Saved");
            refresh();
            return true;
        }
        case b::F3: {
            char name[kNameLen + 1];
            for (int i = 1; i < 1000; ++i) {
                std::snprintf(name, sizeof name, "SONG%03d", i);
                bool used = false;
                for (int k = 0; k < count_ && !used; ++k) used = !std::strcmp(names_[k], name);
                if (!used) break;
            }
            if (app.save(name)) {
                char msg[48];
                std::snprintf(msg, sizeof msg, "Saved as %s", name);
                ui().message(msg);
            }
            refresh();
            return true;
        }
        case b::F4:
            if (app.dirty() && !confirm(b::F4, "Unsaved changes: press NEW again")) return true;
            app.newSong();
            ui().message("New song");
            return true;
        case b::F8:
            if (!count_ || !confirm(b::F8, "Press DELETE again to delete")) return true;
            if (app.removeProject(names_[sel_])) ui().message("Deleted");
            refresh();
            return true;
        default: return false;
    }
}

bool FileScreen::goatButton(uint8_t id) {
    App& app = ui().app();
    char file[kNameCap + 8];
    switch (id) {
        case b::F1:
        case b::EncPush: {
            if (!count_) return true;
            const bool isSong = std::strstr(names_[sel_], ".sng") || std::strstr(names_[sel_], ".SNG");
            if (isSong && app.dirty() && !confirm(b::F1, "Unsaved changes: press IMPORT again")) return true;
            const bool ok = app.importGoat(names_[sel_]);
            reportShown_ = true;
            ui().message(ok ? (isSong ? "Song imported" : "Instrument imported") : "Import failed", 2000);
            return true;
        }
        case b::F2: {
            if (pending_ != b::F2 + 1) {
                // First press: the compatibility check (PLAN.md §16).
                const goat::ExportCheck c = app.checkGoat();
                reportShown_ = false;
                exportCheck_ = c.report;
                pending_ = b::F2 + 1;
                ui().message("Press EXPORT again to write", 3000);
                return true;
            }
            pending_ = 0;
            std::snprintf(file, sizeof file, "%s.sng", app.fileName());
            const bool ok = app.exportGoatSong(file);
            reportShown_ = true;
            ui().message(ok ? "Exported" : "Export failed", 2000);
            refresh();
            return true;
        }
        case b::F3: {
            const Instrument& in = ui().song().inst[ui().inst];
            std::snprintf(file, sizeof file, "%.24s.ins", in.name[0] ? in.name : "INSTR");
            for (char* c = file; *c; ++c)
                if (*c == ' ') *c = '_';
            const bool ok = app.exportGoatInstrument(ui().inst, file);
            reportShown_ = true;
            ui().message(ok ? "Instrument exported" : "Export failed", 2000);
            refresh();
            return true;
        }
        case b::F8: {
            if (!count_ || !confirm(b::F8, "Press DELETE again to delete")) return true;
            char path[96];
            std::snprintf(path, sizeof path, "%s/%s", App::kGoatDir, names_[sel_]);
            if (app.platform().storage->remove(path)) ui().message("Deleted");
            refresh();
            return true;
        }
        default: return false;
    }
}

void FileScreen::labels(const char* out[8]) {
    if (goat_) {
        out[0] = "IMPORT";
        out[1] = pending_ == b::F2 + 1 ? "WRITE .SNG" : "EXPORT SNG";
        out[2] = "EXPORT INS";
        out[4] = "PROJECTS";
    } else {
        out[0] = "LOAD";
        out[1] = "SAVE";
        out[2] = "SAVE NEW";
        out[3] = "NEW";
        out[4] = "GT FILES";
        out[5] = pending_ == b::F6 + 1 ? "WRITE .SID" : "EXPORT SID";
    }
    out[7] = "DELETE";
}

void FileScreen::draw(ot::Gfx& g) {
    App& app = ui().app();
    char buf[80];
    std::snprintf(buf, sizeof buf, "CURRENT  %s%s", app.fileName(), app.dirty() ? "  (modified)" : "");
    g.text(16, kContentY + 4, kText, col::kText, buf);
    if (!app.platform().storage) {
        g.text(16, kContentY + 40, kText, col::kWarn, "No SD card");
        return;
    }
    g.text(16, kContentY + 34, kMono, col::kDim,
           goat_ ? "GOATTRACKER  /SIDTracker/GoatTracker" : "PROJECTS  /SIDTracker/Projects");
    const int rows = 18;
    int top = sel_ - rows / 2;
    if (top > count_ - rows) top = count_ - rows;
    if (top < 0) top = 0;
    for (int i = 0; i < rows && top + i < count_; ++i) {
        const int k = top + i;
        const int y = kContentY + 56 + i * 18;
        if (k == sel_) g.fill(12, y - 1, 400, 18, col::kPanelHi);
        g.text(20, y, kMono, std::strcmp(names_[k], app.fileName()) ? col::kText : col::kGood, names_[k]);
    }
    if (!count_) g.text(20, kContentY + 56, kMono, col::kDim, goat_ ? "(no .sng / .ins files)" : "(no projects yet: SAVE NEW)");

    // Right panel: export check or the last import / export report.
    const goat::Report* r = nullptr;
    const char* title = nullptr;
    if (goat_ && pending_ == b::F2 + 1) {
        r = &exportCheck_;
        title = "GOATTRACKER EXPORT";
    } else if (goat_ && reportShown_) {
        r = &app.goatReport();
        title = "REPORT";
    } else if (!goat_ && pending_ == b::F6 + 1) {
        r = &exportCheck_;
        title = "SID EXPORT (C64)";
    } else if (!goat_ && reportShown_) {
        r = sidFailed_ ? &exportCheck_ : &app.goatReport();
        title = "REPORT";
    }
    if (r) {
        g.fill(424, kContentY + 52, 370, 22 + r->lines * 18, col::kPanel);
        g.text(432, kContentY + 56, kMono, col::kText, title);
        for (int i = 0; i < r->lines; ++i)
            g.text(432, kContentY + 76 + i * 18, kMono, r->line[i][0] == '!' ? col::kWarn : col::kText, r->line[i]);
    } else if (!goat_) {
        g.text(440, kContentY + 56, kMono, col::kDim, "F5: GoatTracker .sng / .ins");
        g.text(440, kContentY + 74, kMono, col::kDim, "F6: export .sid to /SIDTracker/SID");
    }
}

}  // namespace ui
}  // namespace sidt
