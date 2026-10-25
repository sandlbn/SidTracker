// SIDTracker -- pattern command names (commands.h).
#include "tracker/commands/commands.h"

namespace sidt {
namespace cmd {

namespace {
struct Info {
    const char* name;
    const char* param;
};
const Info kInfo[kCount] = {
    {"NONE", ""},
    {"PORTA UP", "speed tbl row"},
    {"PORTA DOWN", "speed tbl row"},
    {"TONE PORTA", "speed tbl row, 00 legato"},
    {"VIBRATO", "speed tbl row, 00 off"},
    {"SET AD", "attack/decay"},
    {"SET SR", "sustain/release"},
    {"SET WAVE", "control bits"},
    {"WAVE PTR", "wave tbl row"},
    {"PULSE PTR", "pulse tbl row"},
    {"FILTER PTR", "filter tbl row"},
    {"FILTER CTRL", "res<<4 | routing"},
    {"CUTOFF", "cutoff high byte"},
    {"SID VOLUME", "0..F, whole chip (3 tracks); per track: Master"},
    {"FUNK TEMPO", "speed tbl row"},
    {"TEMPO", "ticks per row"},
};
}  // namespace

const char* name(uint8_t c) { return c < kCount ? kInfo[c].name : "?"; }
const char* paramHelp(uint8_t c) { return c < kCount ? kInfo[c].param : ""; }
bool usesSpeedTable(uint8_t c) {
    return c == kPortaUp || c == kPortaDown || c == kTonePorta || c == kVibrato || c == kFunkTempo;
}

}  // namespace cmd
}  // namespace sidt
