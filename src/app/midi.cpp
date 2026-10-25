// SIDTracker -- MIDI parser and ASID encoder (midi.h).
#include "app/midi.h"

namespace sidt {
namespace midi {

namespace {
// SID register -> ASID bit position (control registers last).
constexpr int8_t kBitOf[25] = {0, 1, 2, 3, 22, 4, 5, 6, 7, 8, 9, 23, 10, 11, 12, 13, 14, 15, 24, 16, 17, 18, 19, 20, 21};
const uint8_t kCmd[AsidEncoder::kChips] = {0x4E, 0x50};

int dataBytes(uint8_t status) {
    switch (status & 0xF0) {
        case 0xC0:
        case 0xD0: return 1;
        case 0xF0:
            if (status == 0xF1 || status == 0xF3) return 1;
            if (status == 0xF2) return 2;
            return 0;
        default: return 2;
    }
}
}  // namespace

bool Parser::feed(uint8_t b, Msg& m) {
    if (b >= 0xF8) {   // real time: may arrive inside any other message
        m = Msg{b, 0, 0};
        return true;
    }
    if (b == 0xF0) {
        sysex_ = true;
        status_ = 0;
        return false;
    }
    if (b == 0xF7) {
        sysex_ = false;
        return false;
    }
    if (b & 0x80) {
        sysex_ = false;
        status_ = b;
        have_ = 0;
        if (dataBytes(b) == 0) {   // tune request etc.
            m = Msg{b, 0, 0};
            status_ = 0;
            return true;
        }
        return false;
    }
    if (sysex_ || !status_) return false;
    data_[have_++] = b;
    if (have_ < dataBytes(status_)) return false;
    m = Msg{status_, data_[0], have_ > 1 ? data_[1] : uint8_t(0)};
    have_ = 0;
    if (status_ >= 0xF0) status_ = 0;   // system common: no running status
    return true;
}

void AsidEncoder::write(uint8_t chip, uint8_t reg, uint8_t val) {
    if (chip >= kChips || reg > 24) return;
    int bit = kBitOf[reg];
    if ((image_[chip] >> bit) & 1) {
        image_[chip] &= ~(1u << bit);   // the register image value: replaced, not retriggered
    } else if (bit >= 22 && (mask_[chip] >> bit) & 1) {
        // A second write to a control register this frame: the retrigger
        // slot (25-27); a third one replaces the second.
        bit += 3;
    }
    val_[chip][bit] = val;
    mask_[chip] |= 1u << bit;
}

void AsidEncoder::set(uint8_t chip, uint8_t reg, uint8_t val) {
    if (chip >= kChips || reg > 24) return;
    const int bit = kBitOf[reg];
    val_[chip][bit] = val;
    mask_[chip] |= 1u << bit;
    image_[chip] |= 1u << bit;
}

uint32_t AsidEncoder::build(int c, uint8_t* out, uint32_t& taken) {
    taken = 0;
    if (c < 0 || c >= kChips || !mask_[c]) return 0;
    uint8_t* p = out;
    const uint32_t mask = mask_[c];
    uint32_t hi = 0;
    for (int b = 0; b < 28; ++b)
        if (((mask >> b) & 1) && (val_[c][b] & 0x80)) hi |= 1u << b;
    *p++ = 0xF0;
    *p++ = 0x2D;
    *p++ = kCmd[c];
    for (int k = 0; k < 4; ++k) *p++ = uint8_t((mask >> (7 * k)) & 0x7F);
    for (int k = 0; k < 4; ++k) *p++ = uint8_t((hi >> (7 * k)) & 0x7F);
    for (int b = 0; b < 28; ++b)
        if ((mask >> b) & 1) *p++ = uint8_t(val_[c][b] & 0x7F);
    *p++ = 0xF7;
    taken = mask;
    sent_[c] = image_[c];
    mask_[c] = image_[c] = 0;
    return uint32_t(p - out);
}

uint32_t AsidEncoder::flush(uint8_t* out, uint32_t cap) {
    uint32_t n = 0;
    for (int c = 0; c < kChips; ++c) {
        const uint32_t mask = mask_[c];
        if (!mask) continue;
        if (n + kMaxMessage > cap) break;
        uint32_t hi = 0;
        for (int b = 0; b < 28; ++b)
            if (((mask >> b) & 1) && (val_[c][b] & 0x80)) hi |= 1u << b;
        out[n++] = 0xF0;
        out[n++] = 0x2D;
        out[n++] = kCmd[c];
        for (int k = 0; k < 4; ++k) out[n++] = uint8_t((mask >> (7 * k)) & 0x7F);
        for (int k = 0; k < 4; ++k) out[n++] = uint8_t((hi >> (7 * k)) & 0x7F);
        for (int b = 0; b < 28; ++b)
            if ((mask >> b) & 1) out[n++] = uint8_t(val_[c][b] & 0x7F);
        out[n++] = 0xF7;
        mask_[c] = image_[c] = 0;
    }
    return n;
}

uint32_t AsidEncoder::start(uint8_t* out) {
    out[0] = 0xF0, out[1] = 0x2D, out[2] = 0x4C, out[3] = 0xF7;
    return 4;
}

uint32_t AsidEncoder::stop(uint8_t* out) {
    out[0] = 0xF0, out[1] = 0x2D, out[2] = 0x4D, out[3] = 0xF7;
    return 4;
}

}  // namespace midi
}  // namespace sidt
