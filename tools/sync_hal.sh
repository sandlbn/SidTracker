#!/usr/bin/env bash
# Diff the vendored OpenTracker files (src/platform/PROVENANCE.md) against an
# OpenTracker checkout at the recorded commit.
#   tools/sync_hal.sh [OPENTRACKER_REIMPL_DIR] [--commit SHA]
# Exit status 0 = identical except the documented local changes listed below.
set -u
here="$(cd "$(dirname "$0")/.." && pwd)"
ot="${1:-$HOME/Projects/ReversedPolytracker-1.9x/reimpl}"
commit="0849cb0"
[ "${2:-}" = "--commit" ] && commit="${3:?commit}"

repo="$(cd "$ot/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
git -C "$repo" archive "$commit" reimpl | tar -x -C "$tmp" || { echo "cannot read $commit from $repo"; exit 2; }
src="$tmp/reimpl"

# Files with documented local changes (PROVENANCE.md "Local changes").
local_changes="src/platform/display/ft81x.h src/platform/audio/audio_sgtl5000.cpp src/platform/audio/audio_sgtl5000.h src/platform/input/input.cpp src/platform/input/input.h src/platform/board.cpp src/platform/hal.h"

map() {   # sidtracker-path opentracker-path
    printf '%s %s\n' "$1" "$2"
}
pairs() {
    map src/platform/hal_api.h src/core/hal_api.h
    for f in board.cpp board.h k66.h hw_math.h i2c_bus.cpp i2c_bus.h diag.cpp diag.h diag_log.cpp diag_log.h \
             fw_update.cpp fw_update.h midi_ports.cpp midi_ports.h selftest.cpp selftest.h hal.h \
             radio_si4703.cpp radio_si4703.h; do map "src/platform/$f" "src/hal/$f"; done
    for f in telemetry.h telemetry.cpp; do map "src/platform/timer/$f" "src/hal/$f"; done
    for f in ft81x.h ft81x.cpp; do map "src/platform/display/$f" "src/hal/$f"; done
    for f in input.h input.cpp pad_leds.h pad_leds.cpp; do map "src/platform/input/$f" "src/hal/$f"; done
    for f in audio_sgtl5000.h audio_sgtl5000.cpp audio_sgtl5000_dap.h; do map "src/platform/audio/$f" "src/hal/$f"; done
    for f in sdhc.h sdhc.cpp sd_storage.h sd_storage.cpp nvstore_eeprom.h nvstore_eeprom.cpp; do
        map "src/platform/storage/$f" "src/hal/$f"; done
    for f in crc32.h crc32.cpp; do map "src/platform/core/formats/$f" "src/core/formats/$f"; done
    for f in gfx.h gfx.cpp theme.h theme.cpp buttons.h; do map "src/platform/core/ui/$f" "src/core/ui/$f"; done
    for f in "$src"/src/core/ui/fonts/*; do b="$(basename "$f")"; map "src/platform/core/ui/fonts/$b" "src/core/ui/fonts/$b"; done
    for f in "$src"/sim/src/*; do b="$(basename "$f")"; [ "$b" = main.cpp ] || map "sim/src/$b" "sim/src/$b"; done
    map tools/check_image.py tools/check_image.py
    map tests/test.h tests/test.h
    map tests/mem_storage.h tests/mem_storage.h
}

status=0
while read -r ours theirs; do
    if ! diff -q "$here/$ours" "$src/$theirs" >/dev/null 2>&1; then
        case " $local_changes " in
            *" $ours "*) echo "local change: $ours" ;;
            *) echo "DIFFERS:      $ours  (vs $theirs)"; status=1 ;;
        esac
    fi
done < <(pairs)
# Third-party trees: only teensy3/usb_desc.h carries a local change.
while read -r line; do
    case "$line" in
        *"third_party/teensy3/usb_desc.h and "*) echo "local change: third_party/teensy3/usb_desc.h" ;;
        *) echo "DIFFERS:      $line"; status=1 ;;
    esac
done < <(diff -rq "$here/third_party/teensy3" "$src/third_party/teensy3"; diff -rq "$here/third_party/fatfs" "$src/third_party/fatfs")
[ $status = 0 ] && echo "vendored files match $commit (plus the documented local changes)"
exit $status
