// In-memory IStorage fake for host tests.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "hal_api.h"

class MemStorage : public ot::IStorage {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs;

    Handle open(const char* path, OpenMode mode) override {
        std::string p(path);
        auto it = files.find(p);
        if (mode == kRead && it == files.end()) return -1;
        if (mode == kWrite) files[p].clear();
        if (mode == kReadWrite && it == files.end()) files[p];
        Handle h = next_++;
        open_[h] = {p, 0};
        return h;
    }
    int32_t read(Handle h, void* dst, uint32_t len) override {
        auto* o = get(h);
        if (!o) return -1;
        auto& f = files[o->path];
        uint32_t avail = o->pos < f.size() ? uint32_t(f.size() - o->pos) : 0;
        uint32_t n = len < avail ? len : avail;
        // Exercise short reads: never return more than 1000 bytes at once.
        if (n > 1000) n = 1000;
        if (n) std::memcpy(dst, f.data() + o->pos, n);
        o->pos += n;
        return int32_t(n);
    }
    int32_t write(Handle h, const void* src, uint32_t len) override {
        auto* o = get(h);
        if (!o) return -1;
        auto& f = files[o->path];
        if (f.size() < o->pos + len) f.resize(o->pos + len);
        if (len) std::memcpy(f.data() + o->pos, src, len);
        o->pos += len;
        return int32_t(len);
    }
    bool seek(Handle h, uint32_t pos) override {
        auto* o = get(h);
        if (!o) return false;
        o->pos = pos;
        return true;
    }
    int32_t size(Handle h) override {
        auto* o = get(h);
        return o ? int32_t(files[o->path].size()) : -1;
    }
    void close(Handle h) override { open_.erase(h); }
    bool exists(const char* path) override { return files.count(path) || dirs.count(path); }
    bool mkdir(const char* path) override {
        dirs.insert(path);
        return true;
    }
    bool remove(const char* path) override { return files.erase(path) || dirs.erase(path); }
    bool rename(const char* from, const char* to) override {
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(from);
        return true;
    }
    int32_t listDir(const char* path, DirFn fn, void* ctx) override {
        std::string prefix = std::string(path) + "/";
        int32_t n = 0;
        for (auto& kv : files) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = kv.first.substr(prefix.size());
            if (rest.find('/') != std::string::npos) continue;
            DirEntry e{rest.c_str(), false, uint32_t(kv.second.size())};
            ++n;
            if (!fn(e, ctx)) break;
        }
        return n;
    }

private:
    struct Open {
        std::string path;
        uint32_t pos;
    };
    Open* get(Handle h) {
        auto it = open_.find(h);
        return it == open_.end() ? nullptr : &it->second;
    }
    std::map<Handle, Open> open_;
    Handle next_ = 1;
};
