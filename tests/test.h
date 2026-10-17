// Minimal host test framework. Each tests/*_test.cpp is its own binary;
// this header provides main() (one test TU per binary).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace ottest {

struct Case {
    const char* name;
    void (*fn)();
    Case* next;
};
inline Case*& head() {
    static Case* h = nullptr;
    return h;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}
struct Reg {
    Reg(Case* c) {
        Case** p = &head();
        while (*p) p = &(*p)->next;
        *p = c;
    }
};
inline void fail(const char* file, int line, const char* expr) {
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    ++failures();
}

}  // namespace ottest

#define TEST(name)                                                    \
    static void test_##name();                                        \
    static ottest::Case case_##name{#name, &test_##name, nullptr};    \
    static ottest::Reg reg_##name(&case_##name);                      \
    static void test_##name()

#define CHECK(cond)                                                     \
    do {                                                                \
        ++ottest::checks();                                             \
        if (!(cond)) ottest::fail(__FILE__, __LINE__, #cond);           \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        ++ottest::checks();                                                                     \
        auto va_ = (a);                                                                         \
        auto vb_ = (b);                                                                         \
        if (!(va_ == vb_)) {                                                                    \
            ottest::fail(__FILE__, __LINE__, #a " == " #b);                                     \
            std::printf("       got %lld vs %lld (0x%llx vs 0x%llx)\n", (long long)va_, (long long)vb_, \
                        (unsigned long long)va_, (unsigned long long)vb_);                      \
        }                                                                                       \
    } while (0)

#define CHECK_STR(a, b)                                                                  \
    do {                                                                                 \
        ++ottest::checks();                                                              \
        const char* sa_ = (a);                                                           \
        const char* sb_ = (b);                                                           \
        if (std::strcmp(sa_, sb_) != 0) {                                                \
            ottest::fail(__FILE__, __LINE__, #a " == " #b);                              \
            std::printf("       got \"%s\" vs \"%s\"\n", sa_, sb_);                      \
        }                                                                                \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                             \
    do {                                                                                  \
        ++ottest::checks();                                                               \
        double da_ = double(a), db_ = double(b);                                          \
        if (!(std::fabs(da_ - db_) <= double(tol))) {                                     \
            ottest::fail(__FILE__, __LINE__, #a " ~= " #b);                               \
            std::printf("       got %.9g vs %.9g\n", da_, db_);                           \
        }                                                                                 \
    } while (0)

#define CHECK_BYTES(ptr, ...)                                                       \
    do {                                                                            \
        static const uint8_t exp_[] = {__VA_ARGS__};                                \
        ++ottest::checks();                                                         \
        if (std::memcmp((ptr), exp_, sizeof(exp_)) != 0) {                          \
            ottest::fail(__FILE__, __LINE__, "bytes at " #ptr);                     \
            std::printf("       got     ");                                         \
            for (size_t i_ = 0; i_ < sizeof(exp_); ++i_)                            \
                std::printf("%02x ", (unsigned)((const uint8_t*)(ptr))[i_]);        \
            std::printf("\n       expected");                                       \
            for (size_t i_ = 0; i_ < sizeof(exp_); ++i_) std::printf(" %02x", exp_[i_]); \
            std::printf("\n");                                                      \
        }                                                                           \
    } while (0)

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (ottest::Case* c = ottest::head(); c; c = c->next) {
        if (filter && !std::strstr(c->name, filter)) continue;
        int before = ottest::failures();
        c->fn();
        ++run;
        if (ottest::failures() != before) std::printf("[FAIL] %s\n", c->name);
    }
    std::printf("%s: %d tests, %d checks, %d failures\n", argv[0], run, ottest::checks(), ottest::failures());
    return ottest::failures() ? 1 : 0;
}
