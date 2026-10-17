#!/usr/bin/env bash
# Compare SIDTracker's playback of GoatTracker songs with GoatTracker's own
# player (docs/goattracker-format.md "Oracle").
#   tools/oracle/run.sh GT2RELOC SONG.sng... [-- gtcompare options]
# GT2RELOC is GoatTracker's packer/relocator (GPL; run out of process, never
# linked). Songs are packed with -I0 (no packer optimisations): the
# optimised players store pulse widths in fewer bits than the editor plays
# (docs/goattracker-format.md). Traces go to build/oracle/.
set -u
here="$(cd "$(dirname "$0")/../.." && pwd)"
reloc="$1"; shift
songs=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do songs+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
make -s -C "$here/tests" oracle || exit 2
out="$here/build/oracle"; work="$out/work"; mkdir -p "$work"
fail=0
for s in "${songs[@]}"; do
    b="$(basename "$s" .sng)"
    cp "$s" "$work/s.sng"
    # gt2reloc overflows a buffer on long paths: run it on short names.
    (cd "$work" && "$reloc" s.sng s.sid -I0 >/dev/null 2>&1) || { echo "$b: gt2reloc failed"; fail=1; continue; }
    "$out/sidtrace" "$work/s.sid" --frames 3000 > "$out/$b.trace" || { echo "$b: sidtrace failed"; fail=1; continue; }
    "$out/gtcompare" "$s" "$out/$b.trace" "$@" || fail=1
done
exit $fail
