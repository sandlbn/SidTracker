#!/usr/bin/env python3
"""compare_orig.py -- the imported Blast Grid songs against the game's own
.sid: runs both on the oracle's 6502 emulator (build/oracle/sidtrace) and
compares, frame by frame, the note (semitone) each SID 1 voice sounds while
its gate is on, at the best time alignment (within +-24 frames).

    compare_orig.py ORIG.sid IMPORTED_SID_DIR [FRAMES]
"""
import math
import os
import subprocess
import sys

ORDER = ["TITLE", "WORLD1", "WORLD2", "WORLD3", "WORLD4", "WORLD5", "BATTLE", "HURRY", "STAGE_CLEAR",
         "GAME_OVER", "VICTORY", "BOSS", "BOSS_FINAL"]
HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.join(HERE, "..", "..", "build", "oracle", "sidtrace")


def trace(path, song, frames):
    out = subprocess.run([TRACE, path, "--song", str(song), "--frames", str(frames)], capture_output=True,
                         text=True, check=True).stdout
    rows = []
    for ln in out.splitlines():
        if ln.startswith("#") or ":" not in ln:
            continue
        r = [int(x, 16) for x in ln.split(":", 1)[1].split()]
        v = []
        for k in range(3):
            f = r[7 * k] | (r[7 * k + 1] << 8)
            gate = r[7 * k + 4] & 1
            semi = round(12 * math.log2(f * 985248 / 16777216 / 440.0) + 57) if f and gate else None
            v.append(semi)
        rows.append(v)
    return rows


def score(a, b, voice):
    best = (0, 0, 0)
    for shift in range(-24, 25):
        both = match = 0
        for i in range(len(a)):
            j = i + shift
            if j < 0 or j >= len(b):
                continue
            x, y = a[i][voice], b[j][voice]
            if x is None or y is None:
                continue
            both += 1
            match += x == y
        if both and match / both > best[0]:
            best = (match / both, shift, both)
    return best


def main(argv):
    orig, d = argv[0], argv[1]
    frames = int(argv[2]) if len(argv) > 2 else 3000
    for i, name in enumerate(ORDER):
        a = trace(orig, i, frames)
        b = trace(os.path.join(d, name + ".sid"), 0, frames)
        res = [score(a, b, v) for v in (0, 1)]
        print(f"{name:12s} lead {res[0][0] * 100:5.1f}% (shift {res[0][1]:+d}, {res[0][2]} frames)   "
              f"bass {res[1][0] * 100:5.1f}% (shift {res[1][1]:+d}, {res[1][2]} frames)")


if __name__ == "__main__":
    main(sys.argv[1:])
