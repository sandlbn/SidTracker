// asm6502 -- a small two-pass 6502 assembler for SIDTracker's C64 play
// routine (M9, src/compatibility/sid). MIT, written for this project.
//
//   asm6502 IN.s -o OUT.bin [--header OUT.h --name kPrefix]
//
// Syntax (one statement per line, ';' starts a comment):
//   label:              global label; "@name:" is local to the last global
//   NAME = expr         constant
//   .org expr           set the program counter (the first one is the base;
//                       later ones may only move forward, the gap is zeroed)
//   .byte e, "text"     bytes      .word e, ...   little-endian words
//   .res n [, fill]     n bytes    .align n       pad to a multiple of n
//   .export name, ...   symbols written to the C++ header
//   mnemonic operand    #imm  zp  zp,x  zp,y  abs  abs,x  abs,y  (zp,x)
//                       (zp),y  (abs) (jmp)  a / nothing (accumulator)
// Expressions: numbers ($hex, %bin, decimal, 'c'), symbols, * (the pc),
// unary - ~ < (low byte) > (high byte), * / % + - << >> & ^ |, parentheses.
// Zero page addressing is chosen when the operand's value is known in the
// first pass and below $100; otherwise absolute (the size is fixed then).
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum Mode { kImp, kAcc, kImm, kZp, kZpx, kZpy, kAbs, kAbsx, kAbsy, kInd, kIndx, kIndy, kRel, kModes };

struct Op {
    const char* name;
    int code[kModes];   // -1 = not available
};

// clang-format off
//                    imp   acc   imm   zp    zpx   zpy   abs   absx  absy  ind   indx  indy  rel
const Op kOps[] = {
    {"adc", {-1,   -1,   0x69, 0x65, 0x75, -1,   0x6D, 0x7D, 0x79, -1,   0x61, 0x71, -1}},
    {"and", {-1,   -1,   0x29, 0x25, 0x35, -1,   0x2D, 0x3D, 0x39, -1,   0x21, 0x31, -1}},
    {"asl", {-1,   0x0A, -1,   0x06, 0x16, -1,   0x0E, 0x1E, -1,   -1,   -1,   -1,   -1}},
    {"bcc", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x90}},
    {"bcs", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0xB0}},
    {"beq", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0xF0}},
    {"bit", {-1,   -1,   -1,   0x24, -1,   -1,   0x2C, -1,   -1,   -1,   -1,   -1,   -1}},
    {"bmi", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x30}},
    {"bne", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0xD0}},
    {"bpl", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x10}},
    {"brk", {0x00, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"bvc", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x50}},
    {"bvs", {-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x70}},
    {"clc", {0x18, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"cld", {0xD8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"cli", {0x58, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"clv", {0xB8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"cmp", {-1,   -1,   0xC9, 0xC5, 0xD5, -1,   0xCD, 0xDD, 0xD9, -1,   0xC1, 0xD1, -1}},
    {"cpx", {-1,   -1,   0xE0, 0xE4, -1,   -1,   0xEC, -1,   -1,   -1,   -1,   -1,   -1}},
    {"cpy", {-1,   -1,   0xC0, 0xC4, -1,   -1,   0xCC, -1,   -1,   -1,   -1,   -1,   -1}},
    {"dec", {-1,   -1,   -1,   0xC6, 0xD6, -1,   0xCE, 0xDE, -1,   -1,   -1,   -1,   -1}},
    {"dex", {0xCA, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"dey", {0x88, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"eor", {-1,   -1,   0x49, 0x45, 0x55, -1,   0x4D, 0x5D, 0x59, -1,   0x41, 0x51, -1}},
    {"inc", {-1,   -1,   -1,   0xE6, 0xF6, -1,   0xEE, 0xFE, -1,   -1,   -1,   -1,   -1}},
    {"inx", {0xE8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"iny", {0xC8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"jmp", {-1,   -1,   -1,   -1,   -1,   -1,   0x4C, -1,   -1,   0x6C, -1,   -1,   -1}},
    {"jsr", {-1,   -1,   -1,   -1,   -1,   -1,   0x20, -1,   -1,   -1,   -1,   -1,   -1}},
    {"lda", {-1,   -1,   0xA9, 0xA5, 0xB5, -1,   0xAD, 0xBD, 0xB9, -1,   0xA1, 0xB1, -1}},
    {"ldx", {-1,   -1,   0xA2, 0xA6, -1,   0xB6, 0xAE, -1,   0xBE, -1,   -1,   -1,   -1}},
    {"ldy", {-1,   -1,   0xA0, 0xA4, 0xB4, -1,   0xAC, 0xBC, -1,   -1,   -1,   -1,   -1}},
    {"lsr", {-1,   0x4A, -1,   0x46, 0x56, -1,   0x4E, 0x5E, -1,   -1,   -1,   -1,   -1}},
    {"nop", {0xEA, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"ora", {-1,   -1,   0x09, 0x05, 0x15, -1,   0x0D, 0x1D, 0x19, -1,   0x01, 0x11, -1}},
    {"pha", {0x48, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"php", {0x08, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"pla", {0x68, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"plp", {0x28, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"rol", {-1,   0x2A, -1,   0x26, 0x36, -1,   0x2E, 0x3E, -1,   -1,   -1,   -1,   -1}},
    {"ror", {-1,   0x6A, -1,   0x66, 0x76, -1,   0x6E, 0x7E, -1,   -1,   -1,   -1,   -1}},
    {"rti", {0x40, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"rts", {0x60, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"sbc", {-1,   -1,   0xE9, 0xE5, 0xF5, -1,   0xED, 0xFD, 0xF9, -1,   0xE1, 0xF1, -1}},
    {"sec", {0x38, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"sed", {0xF8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"sei", {0x78, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"sta", {-1,   -1,   -1,   0x85, 0x95, -1,   0x8D, 0x9D, 0x99, -1,   0x81, 0x91, -1}},
    {"stx", {-1,   -1,   -1,   0x86, -1,   0x96, 0x8E, -1,   -1,   -1,   -1,   -1,   -1}},
    {"sty", {-1,   -1,   -1,   0x84, 0x94, -1,   0x8C, -1,   -1,   -1,   -1,   -1,   -1}},
    {"tax", {0xAA, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"tay", {0xA8, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"tsx", {0xBA, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"txa", {0x8A, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"txs", {0x9A, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
    {"tya", {0x98, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1}},
};
// clang-format on

const Op* findOp(const std::string& n) {
    for (const Op& o : kOps)
        if (n == o.name) return &o;
    return nullptr;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Splits on commas outside quotes and parentheses.
std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool q = false;
    for (char c : s) {
        if (c == '"') q = !q;
        if (!q && c == '(') ++depth;
        if (!q && c == ')') --depth;
        if (!q && depth == 0 && c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

struct Error {
    std::string msg;
};

class Asm {
public:
    bool run(const std::vector<std::string>& lines) {
        lines_ = lines;
        for (pass_ = 1; pass_ <= 2; ++pass_) {
            pc_ = 0;
            base_ = -1;
            out_.clear();
            scope_.clear();
            for (line_ = 0; line_ < int(lines_.size()); ++line_) {
                try {
                    statement(lines_[line_]);
                } catch (const Error& e) {
                    std::fprintf(stderr, "line %d: %s\n  %s\n", line_ + 1, e.msg.c_str(), lines_[line_].c_str());
                    return false;
                }
            }
        }
        return true;
    }
    const std::vector<uint8_t>& out() const { return out_; }
    int base() const { return base_ < 0 ? 0 : base_; }
    const std::vector<std::string>& exports() const { return exports_; }
    long symbol(const std::string& n) const {
        auto it = syms_.find(n);
        return it == syms_.end() ? -1 : it->second;
    }

private:
    // --- expressions -------------------------------------------------------
    struct Parser {
        Asm& a;
        const std::string& s;
        size_t i = 0;
        bool known = true;
        void ws() {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        }
        bool eat(const char* t) {
            ws();
            const size_t n = std::strlen(t);
            if (s.compare(i, n, t) == 0) {
                i += n;
                return true;
            }
            return false;
        }
        long primary() {
            ws();
            if (i >= s.size()) throw Error{"expression expected"};
            const char c = s[i];
            if (c == '(') {
                ++i;
                const long v = expr();
                if (!eat(")")) throw Error{"')' expected"};
                return v;
            }
            if (c == '-') { ++i; return -unary(); }
            if (c == '~') { ++i; return ~unary(); }
            if (c == '<') { ++i; return unary() & 0xFF; }
            if (c == '>') { ++i; return (unary() >> 8) & 0xFF; }
            if (c == '*') { ++i; return a.pc_; }
            if (c == '$') {
                ++i;
                size_t st = i;
                while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i]))) ++i;
                if (st == i) throw Error{"hex digits expected"};
                return std::strtol(s.substr(st, i - st).c_str(), nullptr, 16);
            }
            if (c == '%') {
                ++i;
                size_t st = i;
                while (i < s.size() && (s[i] == '0' || s[i] == '1')) ++i;
                if (st == i) throw Error{"binary digits expected"};
                return std::strtol(s.substr(st, i - st).c_str(), nullptr, 2);
            }
            if (c == '\'') {
                if (i + 2 >= s.size() || s[i + 2] != '\'') throw Error{"bad character constant"};
                const long v = static_cast<unsigned char>(s[i + 1]);
                i += 3;
                return v;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t st = i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                return std::strtol(s.substr(st, i - st).c_str(), nullptr, 10);
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '@') {
                size_t st = i++;
                while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
                const std::string name = a.qualify(s.substr(st, i - st));
                auto it = a.syms_.find(name);
                if (it == a.syms_.end()) {
                    if (a.pass_ == 2) throw Error{"undefined symbol " + name};
                    known = false;
                    return 0x1000;   // placeholder: forces absolute addressing
                }
                return it->second;
            }
            throw Error{std::string("unexpected '") + c + "'"};
        }
        long unary() { return primary(); }
        long mul() {
            long v = unary();
            for (;;) {
                ws();
                if (i < s.size() && s[i] == '*') { ++i; v *= unary(); }
                else if (i < s.size() && s[i] == '/') {
                    ++i;
                    const long d = unary();
                    if (!d) { if (a.pass_ == 2) throw Error{"division by zero"}; v = 0; }
                    else v /= d;
                } else if (i < s.size() && s[i] == '%') {
                    ++i;
                    const long d = unary();
                    v = d ? v % d : 0;
                } else return v;
            }
        }
        long add() {
            long v = mul();
            for (;;) {
                ws();
                if (i < s.size() && s[i] == '+') { ++i; v += mul(); }
                else if (i < s.size() && s[i] == '-') { ++i; v -= mul(); }
                else return v;
            }
        }
        long shift() {
            long v = add();
            for (;;) {
                if (eat("<<")) v <<= add();
                else if (eat(">>")) v >>= add();
                else return v;
            }
        }
        long band() {
            long v = shift();
            for (;;) {
                ws();
                if (i < s.size() && s[i] == '&') { ++i; v &= shift(); }
                else return v;
            }
        }
        long bxor() {
            long v = band();
            for (;;) {
                ws();
                if (i < s.size() && s[i] == '^') { ++i; v ^= band(); }
                else return v;
            }
        }
        long expr() {
            long v = bxor();
            for (;;) {
                ws();
                if (i < s.size() && s[i] == '|') { ++i; v |= bxor(); }
                else return v;
            }
        }
    };

    long eval(const std::string& e, bool* known = nullptr) {
        Parser p{*this, e};
        const long v = p.expr();
        p.ws();
        if (p.i != e.size()) throw Error{"junk after expression: " + e.substr(p.i)};
        if (known) *known = p.known;
        return v;
    }

    std::string qualify(const std::string& n) const { return n[0] == '@' ? scope_ + n : n; }

    void define(const std::string& raw, long v) {
        const std::string n = qualify(raw);
        if (pass_ == 1) {
            if (syms_.count(n)) throw Error{"duplicate symbol " + n};
            syms_[n] = v;
        } else if (syms_[n] != v) {
            throw Error{"symbol " + n + " changed between passes"};
        }
    }

    void emit(long b) {
        if (base_ < 0) throw Error{"code before .org"};
        out_.push_back(uint8_t(b));
        ++pc_;
    }

    // --- statements --------------------------------------------------------
    void statement(std::string s) {
        // Strip the comment (not inside quotes or a character constant).
        bool q = false;
        for (size_t k = 0; k < s.size(); ++k) {
            if (s[k] == '"') q = !q;
            if (!q && s[k] == '\'' && k + 2 < s.size() && s[k + 2] == '\'') { k += 2; continue; }
            if (!q && s[k] == ';') { s.resize(k); break; }
        }
        s = trim(s);
        if (s.empty()) return;
        // Label.
        size_t k = 0;
        while (k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || s[k] == '_' || s[k] == '@')) ++k;
        if (k > 0 && k < s.size() && s[k] == ':') {
            const std::string name = s.substr(0, k);
            if (name[0] != '@') scope_ = name;
            define(name, pc_);
            s = trim(s.substr(k + 1));
            if (s.empty()) return;
        }
        // Constant.
        const size_t eq = s.find('=');
        if (eq != std::string::npos && s[0] != '.' && s.find('"') == std::string::npos) {
            const std::string name = trim(s.substr(0, eq));
            if (name == "*") { org(eval(trim(s.substr(eq + 1)))); return; }
            define(name, eval(trim(s.substr(eq + 1))));
            return;
        }
        std::string word, rest;
        const size_t sp = s.find_first_of(" \t");
        word = lower(sp == std::string::npos ? s : s.substr(0, sp));
        rest = sp == std::string::npos ? "" : trim(s.substr(sp));
        if (word[0] == '.') { directive(word, rest); return; }
        const Op* op = findOp(word);
        if (!op) throw Error{"unknown mnemonic " + word};
        instruction(*op, rest);
    }

    void org(long v) {
        if (v < 0 || v > 0xFFFF) throw Error{"bad .org"};
        if (base_ < 0) {
            base_ = int(v);
            pc_ = v;
            return;
        }
        if (v < pc_) throw Error{".org moves backwards"};
        while (pc_ < v) emit(0);
    }

    void directive(const std::string& d, const std::string& rest) {
        if (d == ".org") { org(eval(rest)); return; }
        if (d == ".byte" || d == ".word") {
            for (const std::string& a : splitArgs(rest)) {
                if (d == ".byte" && a.size() >= 2 && a.front() == '"' && a.back() == '"') {
                    for (size_t k = 1; k + 1 < a.size(); ++k) emit(static_cast<unsigned char>(a[k]));
                    continue;
                }
                const long v = eval(a);
                if (d == ".byte") {
                    if (pass_ == 2 && (v < -128 || v > 255)) throw Error{"byte out of range: " + a};
                    emit(v & 0xFF);
                } else {
                    emit(v & 0xFF);
                    emit((v >> 8) & 0xFF);
                }
            }
            return;
        }
        if (d == ".res") {
            const auto a = splitArgs(rest);
            if (a.empty()) throw Error{".res needs a count"};
            const long n = eval(a[0]);
            const long fill = a.size() > 1 ? eval(a[1]) : 0;
            if (n < 0) throw Error{"negative .res"};
            for (long k = 0; k < n; ++k) emit(fill);
            return;
        }
        if (d == ".align") {
            const long n = eval(rest);
            if (n <= 0) throw Error{"bad .align"};
            while (pc_ % n) emit(0);
            return;
        }
        if (d == ".export") {
            if (pass_ == 1)
                for (const std::string& a : splitArgs(rest)) exports_.push_back(a);
            return;
        }
        throw Error{"unknown directive " + d};
    }

    void instruction(const Op& op, const std::string& rest) {
        const std::string r = lower(rest);
        auto has = [&](Mode m) { return op.code[m] >= 0; };
        if (r.empty() || r == "a") {
            if (has(kImp)) { emit(op.code[kImp]); return; }
            if (has(kAcc)) { emit(op.code[kAcc]); return; }
            throw Error{"operand expected"};
        }
        if (has(kRel)) {
            const long t = eval(rest);
            const long off = t - (pc_ + 2);
            if (pass_ == 2 && (off < -128 || off > 127)) throw Error{"branch out of range (" + std::to_string(off) + ")"};
            emit(op.code[kRel]);
            emit(off & 0xFF);
            return;
        }
        if (rest[0] == '#') {
            if (!has(kImm)) throw Error{"no immediate mode"};
            const long v = eval(rest.substr(1));
            if (pass_ == 2 && (v < -128 || v > 255)) throw Error{"immediate out of range"};
            emit(op.code[kImm]);
            emit(v & 0xFF);
            return;
        }
        auto endsWith = [&](const char* t) {
            const size_t n = std::strlen(t);
            return r.size() >= n && r.compare(r.size() - n, n, t) == 0;
        };
        if (rest[0] == '(') {
            std::string compact;
            for (char c : r)
                if (!std::isspace(static_cast<unsigned char>(c))) compact += c;
            auto cend = [&](const char* t) {
                const size_t n = std::strlen(t);
                return compact.size() >= n && compact.compare(compact.size() - n, n, t) == 0;
            };
            if (cend("),y")) {
                const std::string in = rest.substr(1, rest.rfind(')') - 1);
                zpOnly(op, kIndy, eval(in));
                return;
            }
            if (cend(",x)")) {
                const std::string in = rest.substr(1, rest.find_last_of(',') - 1);
                zpOnly(op, kIndx, eval(in));
                return;
            }
            if (has(kInd)) {
                const long v = eval(rest.substr(1, rest.rfind(')') - 1));
                emit(op.code[kInd]);
                emit(v & 0xFF);
                emit((v >> 8) & 0xFF);
                return;
            }
        }
        Mode zm = kZp, am = kAbs;
        std::string e = rest;
        if (endsWith(",x")) { zm = kZpx; am = kAbsx; e = trim(rest.substr(0, rest.size() - 2)); }
        else if (endsWith(",y")) { zm = kZpy; am = kAbsy; e = trim(rest.substr(0, rest.size() - 2)); }
        bool known = true;
        const long v = eval(e, &known);
        // The addressing size is decided in pass 1 and kept.
        bool useZp;
        if (pass_ == 1) {
            useZp = known && v >= 0 && v < 0x100 && has(zm);
            zpChoice_[line_] = useZp;
        } else {
            useZp = zpChoice_[line_];
        }
        if (!useZp && !has(am)) {
            if (has(zm)) throw Error{"operand must be on the zero page"};
            throw Error{"addressing mode not available"};
        }
        if (pass_ == 2 && (v < 0 || v > 0xFFFF || (useZp && v > 0xFF))) throw Error{"address out of range"};
        if (useZp) {
            emit(op.code[zm]);
            emit(v & 0xFF);
        } else {
            emit(op.code[am]);
            emit(v & 0xFF);
            emit((v >> 8) & 0xFF);
        }
    }

    void zpOnly(const Op& op, Mode m, long v) {
        if (op.code[m] < 0) throw Error{"addressing mode not available"};
        if (pass_ == 2 && (v < 0 || v > 0xFF)) throw Error{"indirect operand must be on the zero page"};
        emit(op.code[m]);
        emit(v & 0xFF);
    }

    std::vector<std::string> lines_;
    std::map<std::string, long> syms_;
    std::map<int, bool> zpChoice_;
    std::vector<uint8_t> out_;
    std::vector<std::string> exports_;
    std::string scope_;
    long pc_ = 0;
    int base_ = -1;
    int pass_ = 1;
    int line_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::string in, out, header, name = "kAsm";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out = argv[++i];
        else if (a == "--header" && i + 1 < argc) header = argv[++i];
        else if (a == "--name" && i + 1 < argc) name = argv[++i];
        else in = a;
    }
    if (in.empty()) {
        std::fprintf(stderr, "usage: asm6502 IN.s [-o OUT.bin] [--header OUT.h --name kPrefix]\n");
        return 2;
    }
    std::ifstream f(in);
    if (!f) {
        std::perror(in.c_str());
        return 1;
    }
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) lines.push_back(l);
    Asm a;
    if (!a.run(lines)) return 1;
    if (!out.empty()) {
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char*>(a.out().data()), std::streamsize(a.out().size()));
    }
    if (!header.empty()) {
        std::ostringstream h;
        h << "// Generated by tools/asm6502 from " << in.substr(in.find_last_of('/') + 1) << " -- do not edit.\n";
        h << "#pragma once\n\n#include <cstdint>\n\n";
        char buf[64];
        std::snprintf(buf, sizeof buf, "constexpr uint16_t %sBase = 0x%04X;\n", name.c_str(), unsigned(a.base()));
        h << buf;
        std::snprintf(buf, sizeof buf, "constexpr uint16_t %sSize = %u;\n", name.c_str(), unsigned(a.out().size()));
        h << buf;
        for (const std::string& e : a.exports()) {
            const long v = a.symbol(e);
            if (v < 0) {
                std::fprintf(stderr, "export of undefined symbol %s\n", e.c_str());
                return 1;
            }
            std::snprintf(buf, sizeof buf, "constexpr uint16_t %s_%s = 0x%04X;\n", name.c_str(), e.c_str(), unsigned(v));
            h << buf;
        }
        h << "constexpr uint8_t " << name << "Code[" << a.out().size() << "] = {";
        for (size_t i = 0; i < a.out().size(); ++i) {
            if (i % 16 == 0) h << "\n   ";
            std::snprintf(buf, sizeof buf, " 0x%02X,", a.out()[i]);
            h << buf;
        }
        h << "\n};\n";
        std::ofstream o(header);
        o << h.str();
    }
    std::printf("%s: %u bytes at $%04X\n", in.c_str(), unsigned(a.out().size()), unsigned(a.base()));
    return 0;
}
