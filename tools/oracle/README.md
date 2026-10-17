# GoatTracker oracle

Compares SIDTracker's import + playback of GoatTracker songs with GoatTracker's
own player (docs/goattracker-format.md "Oracle"). Host-only tools, MIT:

- `sidtrace.cpp`: a 6502 emulator that runs a PSID's init and play routines and
  prints the SID registers after every frame.
- `gtcompare.cpp`: imports a `.sng`, plays it with SIDTracker's player and
  compares tick for tick with a sidtrace log (`--reg NAME`, `--trace FROM TO`
  for details).
- `sidt2sng.cpp`: SIDTracker project -> `.sng` with the app's exporter.
- `sng2sidt.cpp`: GoatTracker song (optionally a subtune) -> SIDTracker project.
- `sidcompare.cpp`: M9 check of the `.sid` export -- exports a song, runs the
  file on the 6502 emulator and compares every frame with SIDTracker's
  player (docs/sid-export.md).
- `cpu6502.h`: the 6502 emulator shared by sidtrace and sidcompare.
- `run.sh GT2RELOC SONG.sng...`: packs each song with GoatTracker's
  `gt2reloc -I0` (GPL; run as a separate program, never linked), traces and
  compares it. GoatTracker 2.77 is in most Linux distributions (`goattracker`).

```
make -C tests oracle
tools/oracle/run.sh /usr/bin/gt2reloc fixtures/private/goattracker/*.sng
```

`gt2reloc` overflows a buffer on long paths; run.sh copies songs to short names.
