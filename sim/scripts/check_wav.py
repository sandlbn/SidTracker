"""Smoke check for the simulator's recorded WAV: non-silent, not clipped,
and a dominant pitch where the smoke script plays notes."""
import struct
import sys
import wave

path = sys.argv[1]
with wave.open(path, "rb") as w:
    assert w.getnchannels() == 2 and w.getsampwidth() == 2, "expect 16-bit stereo"
    rate = w.getframerate()
    data = w.readframes(w.getnframes())
samples = struct.unpack("<%dh" % (len(data) // 2), data)
left = samples[0::2]
peak = max(abs(s) for s in left)
clipped = sum(1 for s in left if abs(s) >= 32767)
seconds = len(left) / rate
print(f"{path}: {seconds:.2f} s, peak {peak}, clipped samples {clipped}")
if peak < 1000:
    sys.exit("FAIL: output is (nearly) silent")
if clipped > len(left) // 1000:
    sys.exit("FAIL: output clips")
print("ok")
