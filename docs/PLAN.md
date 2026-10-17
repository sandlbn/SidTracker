# SID Tracker for Original Polyend Tracker — Agent Implementation Plan

(Owner-supplied plan, 2026-10-16. Source of truth for scope; see
docs/APPROACH.md for how it maps onto the existing OpenTracker work.)

## 1. Goal

Develop an alternative firmware/application for the original Polyend Tracker
that turns the hardware into a focused dual-SID (2×SID / 6-voice) tracker and
performance instrument.

The project should:

* Run on the original Tracker hardware and its NXP Kinetis K66 / Cortex-M4F platform.
* Provide six SID voices: three voices for SID1 and three for SID2.
* Support selectable 6581 / 8580 behavior.
* Use a clean tracker-first UI rather than trying to display all synthesis parameters at once.
* Provide a dedicated Perform page for live voice and filter manipulation.
* Reuse the reverse-engineered Polyend hardware/firmware interfaces where practical.
* Aim for meaningful GoatTracker compatibility, especially its instrument/table concepts and song import/export.
* Leave room for software synthesis, PCM drums, and extended SID features after the basic SID tracker is stable.

The first objective is not perfect C64 emulation. The objective is a usable musical instrument.

## 2. Design Principles

### Tracker first

The Pattern screen must remain sparse and readable.

Do not place ADSR, filters, waveforms, mixers, scopes, modulation matrices, and
voice controls on the Pattern screen.

Primary Pattern view:

```
 SID TRACKER        SONG 01   PAT 03    BPM 150   SPD 06

     SID1                         SID2
     CH1        CH2        CH3    CH4        CH5        CH6
00 | C-4 01 00 | --- -- -- | --- -- -- | G-3 02 00 | --- -- -- | --- -- --
01 | --- -- -- | --- -- -- | --- -- -- | --- -- -- | --- -- -- | --- -- --
02 | D-4 01 00 | C-3 03 00 | --- -- -- | A-3 02 00 | --- -- -- | --- -- --
03 | --- -- -- | --- -- -- | --- -- -- | --- -- -- | --- -- -- | --- -- --
04 | E-4 01 01 | --- -- -- | G-2 04 00 | B-3 02 00 | --- -- -- | --- -- --
```

### Separate editing from performance

Use distinct screens:

1. Pattern — notes, instruments and commands.
2. Instrument — SID instrument definition and GoatTracker-style tables.
3. Perform — live control of six voices and the two SID engines.
4. Song — pattern/order arrangement.
5. Settings — SID model, clock, audio and compatibility settings.

### Preserve SID character

The basic engine should behave like a SID rather than a generic subtractive
synth wearing a SID UI.

Extended functionality should be explicitly separated from an
Authentic/Compatible mode.

## 3. Hardware Investigation

Before implementing major features, the agent must document the existing
reverse-engineering work.

Create:

```
docs/
  hardware.md
  firmware-map.md
  display.md
  input.md
  audio.md
  storage.md
```

Determine and document:

* LCD framebuffer format and update mechanism.
* Pad matrix/input scanning.
* Button handling.
* Jog wheel/encoder handling.
* Audio DAC/codec interface.
* Audio sample rate and DMA path.
* SD-card interface.
* Existing filesystem support.
* Timer resources.
* DMA resources.
* Memory map.
* Available RAM after required hardware services.
* Firmware entry points that can be reused.
* Whether the existing bootloader/update mechanism can safely load the project.

Do not rewrite working hardware support without a reason.

## 4. Architecture

Use a small layered architecture.

```
                    UI
                     |
             Tracker Engine
                     |
          Event / Register Stream
             /               \
        SID Engine 1       SID Engine 2
        voices 1-3         voices 4-6
             \               /
                  Mixer
                    |
              Optional FX
                    |
               Audio DMA
```

Suggested source tree:

```
src/
  platform/
    display/
    input/
    audio/
    storage/
    timer/

  tracker/
    song/
    pattern/
    playback/
    commands/

  sid/
    sid.cpp
    voice.cpp
    oscillator.cpp
    envelope.cpp
    filter.cpp
    tables.cpp

  compatibility/
    goat/
      import.cpp
      export.cpp
      instrument.cpp
      commands.cpp

  ui/
    pattern/
    instrument/
    perform/
    song/
    settings/

  audio/
    mixer.cpp
    pcm.cpp
    effects.cpp

  app/
    main.cpp
```

Keep platform-specific code separate from musical logic.

## 5. SID Engine

### Initial engine

Implement:

* 2 SID instances.
* 3 voices per SID.
* Triangle.
* Saw.
* Pulse.
* Noise.
* ADSR.
* Pulse width.
* Hard sync.
* Ring modulation.
* Test bit behavior where useful.
* SID filter.
* Master volume.

Support configuration:

```
SID1 = 6581 / 8580
SID2 = 6581 / 8580
```

Mixed configurations such as:

```
SID1 = 6581
SID2 = 8580
```

must be allowed.

### Timing

Separate:

* tracker tick timing,
* SID/register update timing,
* audio sample generation.

Do not make UI frame rate determine synthesis timing.

Use fixed-point arithmetic where it materially reduces CPU cost without
degrading sound.

## 6. CPU Budget and Profiling

The target is a 180 MHz Cortex-M4F, so profiling is mandatory.

Instrument the audio path using the ARM cycle counter where possible.

Measure:

```
SID1 voices
SID1 filter
SID2 voices
SID2 filter
mixer
PCM
effects
total audio callback
```

Create a benchmark mode that reports:

```
Audio buffer period:
Cycles available:
Cycles consumed:
Peak cycles:
CPU utilization:
Audio underruns:
```

Define a hard rule:

New DSP functionality cannot be merged if it causes audio underruns in the
supported configuration.

Optimize only after measuring.

## 7. Pattern Model

Start with six logical SID tracks.

Each row should contain approximately:

```
NOTE | INSTRUMENT | COMMAND
```

Do not initially copy every Polyend Tracker column.

The data model should remain extensible enough to add a second effect/command
column later.

Example:

```
C-4 01 00
--- -- --
D#4 01 A3
C-3 03 F2
```

The six channels map naturally to:

```
1 -> SID1 voice 1
2 -> SID1 voice 2
3 -> SID1 voice 3

4 -> SID2 voice 1
5 -> SID2 voice 2
6 -> SID2 voice 3
```

## 8. GoatTracker Compatibility

Treat GoatTracker compatibility as a first-class architectural requirement.

Research and document the exact GoatTracker song/instrument representation
before implementing import.

Create:

```
docs/goattracker-format.md
docs/goattracker-mapping.md
```

Map GoatTracker concepts into the internal model.

Important concepts include:

* ADSR.
* Wave table.
* Pulse table.
* Filter table.
* Speed table.
* Gate behavior.
* Vibrato.
* Portamento.
* Arpeggio behavior.
* Pattern commands.
* Order lists.
* Instruments.

Do not simply translate rendered audio.

Preserve the musical/programming structure whenever possible.

### Compatibility levels

Define three levels.

**Level 1 — Instrument compatibility.** Import GoatTracker SID instruments and tables.

**Level 2 — Pattern compatibility.** Import notes, instruments and supported pattern commands.

**Level 3 — Song compatibility.** Import complete GoatTracker songs including order lists.

Unsupported behavior must generate a warning rather than silently producing
different music.

Example:

```
Import completed.

97% commands mapped directly.
2 commands approximated.
1 unsupported command.

View report >
```

## 9. Instrument Editor

The Instrument screen should expose the SID programming model without
overcrowding the Pattern view.

Pages/tabs:

```
GENERAL
ADSR
WAVE
PULSE
FILTER
TABLES
```

The Tables page is especially important for GoatTracker users.

Provide editors for:

```
Wave Table
Pulse Table
Filter Table
Speed Table
```

The jog wheel should be the primary value editor.

Pads can provide shortcuts for:

* waveform selection,
* octave/note preview,
* table operations,
* copy/paste,
* instrument audition.

## 10. Perform Screen

The Perform screen is where the six voices become visually explicit.

Layout concept:

```
SID1                         SID2

VOICE 1   M S   WAVE         VOICE 4   M S   WAVE
          CUT RES PW                   CUT RES PW

VOICE 2   M S   WAVE         VOICE 5   M S   WAVE
          CUT RES PW                   CUT RES PW

VOICE 3   M S   WAVE         VOICE 6   M S   WAVE
          CUT RES PW                   CUT RES PW
```

Functions:

* Mute.
* Solo.
* Waveform changes.
* Pulse width.
* Filter cutoff.
* Resonance.
* SID balance.
* Optional pan in extended mode.
* Live recording of parameter changes.

The Perform page can later become the main surface for automation recording.

## 11. Authentic and Extended Modes

Provide two conceptual modes.

### Authentic SID

Designed for compatibility.

Restrict behavior to what can reasonably map to SID/GoatTracker concepts.

### Extended SID

Allow modern features such as:

* stereo voice positioning,
* waveform morphing,
* additional LFOs,
* smoother parameter automation,
* detune,
* unison,
* optional per-voice processing,
* software effects.

Never make an extended feature silently break GoatTracker export.

The UI should indicate when a song contains non-exportable features.

## 12. PCM and Software Synth

Do not implement this during the first SID milestone.

After dual-SID playback is stable, investigate spare CPU and memory.

Potential additional engine:

```
PCM / SOFTWARE
```

Possible uses:

* kick,
* snare,
* hi-hat,
* sampled drums,
* short one-shots,
* simple wavetable oscillator.

The initial target should be SID + lightweight drums, not a large
general-purpose synthesizer.

A later software voice could intentionally complement SID rather than
duplicate it.

## 13. Audio Effects

Effects are optional and must come after stable dual-SID operation.

Candidate order:

1. simple delay,
2. chorus,
3. lightweight reverb.

Effects must have profiling results.

Allow them to be disabled globally.

## 14. Song Screen

Keep arrangement simple.

Example:

```
POS   PATTERN   TRANSPOSE   REPEAT

00      00          0          1
01      01          0          1
02      02         +12         1
03      03          0          2
```

For GoatTracker imports, retain enough metadata to reconstruct the original
order structure where possible.

## 15. File Format

Use a native project format that can evolve independently of GoatTracker.

Prefer a versioned binary format or compact structured representation suitable
for an embedded device.

Conceptually:

```
SIDT
version
song metadata
SID configuration
order list
patterns
instruments
tables
automation
samples
extensions
```

Every file must contain a format version.

Never depend on C/C++ structure layout for persistent storage.

## 16. Import / Export

Target:

```
Load
 ├─ SID Tracker Project
 ├─ GoatTracker Song
 └─ GoatTracker Instrument

Save
 ├─ SID Tracker Project
 ├─ Export GoatTracker Song
 └─ Export GoatTracker Instrument
```

Export should perform a compatibility check first.

Example:

```
GOATTRACKER EXPORT

✓ Notes
✓ Instruments
✓ Wave tables
✓ Pulse tables
✓ Filter tables
✓ Speed tables

! Stereo panning ignored
! Extended LFO 2 cannot be exported
```

## 17. Development Milestones

### M0 — Platform bring-up

Goal:

```
Boot -> Display -> Input -> Audio
```

Acceptance:

* Custom code boots.
* Screen can be updated.
* Buttons/pads/encoder work.
* Stable audio tone can be produced.
* SD files can be read.

### M1 — One SID voice

Goal:

```
Pattern -> oscillator -> DAC
```

Acceptance:

* Notes play accurately.
* Basic waveform selection works.
* ADSR works.
* No audio underruns.

### M2 — One complete SID

Implement:

* 3 voices.
* sync.
* ring modulation.
* noise.
* filter.
* pulse width.

Acceptance:

* Stable three-voice playback.
* CPU profiling available.

### M3 — Dual SID

Implement six voices and two filters.

Acceptance:

* Six simultaneous voices.
* Independent SID models.
* 6581/8580 selection.
* No underruns.

### M4 — Tracker

Implement:

* pattern editing,
* instruments,
* commands,
* playback,
* song order.

At this point the device must already be usable to compose a complete SID track.

### M5 — GoatTracker instruments

Implement table editors and instrument import.

### M6 — GoatTracker songs

Implement patterns/order lists and compatibility reporting.

Use known GoatTracker songs as regression fixtures.

### M7 — Perform

Add live six-voice performance UI and automation.

### M8 — Extended engine

Only after profiling:

* PCM drums,
* effects,
* extended modulation,
* stereo enhancements.

### M9 — SID file export (owner addition, 2026-10-16)

Export songs as real `.sid` files (PSID v3, second SID at $D420 for dual-SID
songs) with SIDTracker's own 6502 play routine: compact song data
(patterns, order lists, instruments, tables) interpreted on the C64, not a
register dump. MIT, written from SIDTracker's playback semantics
(docs/commands.md); GoatTracker's GPL player is never used (DECISIONS.md 2).
Verified frame for frame against SIDTracker's player with the 6502 tracer
in tools/oracle. Done last, after M7 and M8.

## 18. Testing Strategy

Create host-side tests wherever possible.

Test:

* oscillator phase.
* ADSR state transitions.
* table execution.
* tracker timing.
* command decoding.
* GoatTracker parsing.
* GoatTracker export/import round trips.
* project serialization.

Maintain reference fixtures:

```
tests/data/goattracker/
tests/data/projects/
tests/audio/reference/
```

For DSP regressions, optionally render short deterministic audio buffers and
compare them numerically.

Do not rely only on listening tests.

## 19. Agent Workflow

The coding agent should work incrementally.

For every task:

1. Inspect the existing reverse-engineered code before proposing replacements.
2. Identify the smallest subsystem involved.
3. Write or update tests where possible.
4. Implement the minimal change.
5. Build.
6. Run host tests.
7. Run hardware tests when required.
8. Collect performance numbers for realtime/DSP changes.
9. Update documentation.
10. Commit a small coherent change.

The agent must not perform broad rewrites merely to make the architecture cleaner.

Preserve verified hardware knowledge.

## 20. Agent Rules

The agent should follow these rules:

**Never guess hardware behavior.** If an MMIO register, interrupt, DMA channel,
display command or codec operation is not understood, document the uncertainty
and investigate it.

**Preserve known-good code.** Reverse-engineered working code is evidence.
Refactor only with tests or hardware verification.

**Keep realtime code deterministic.** Avoid:

* dynamic allocation in the audio callback,
* filesystem operations in the audio callback,
* UI rendering in audio code,
* unbounded loops,
* locks that can block audio.

**Measure DSP.** Never claim something is too expensive or inexpensive without
measuring it on the K66.

**Compatibility before extensions.** Do not implement modern synthesis features
while fundamental GoatTracker/SID behavior remains incomplete.

## 21. First Agent Tasks

The initial agent should perform these tasks in order:

### Task 1 — Repository audit

Produce:

```
docs/current-state.md
```

Describe:

* build system,
* firmware layout,
* reverse-engineered subsystems,
* known hardware,
* unknown hardware,
* reusable code,
* current boot process.

### Task 2 — Audio path audit

Trace:

```
application
   ↓
audio buffers
   ↓
DMA
   ↓
codec/DAC
```

Document buffer size, sample rate and timing budget.

### Task 3 — Minimal synth test

Produce one controllable oscillator using existing audio output.

Map:

```
pads -> note
encoder -> pitch
```

### Task 4 — SID voice

Replace the test oscillator with one SID-style voice.

### Task 5 — Minimal tracker

Implement:

```
NOTE | INST | CMD
```

for one channel.

### Task 6 — Three voices

Turn the engine into one complete SID.

### Task 7 — Six voices

Instantiate SID2.

Only after these steps should the agent begin full UI and GoatTracker work.

## 22. Definition of the First Useful Release

Version 0.1 should already be a musical instrument.

Required:

* six SID voices,
* 2×SID architecture,
* 6581/8580 selection,
* pattern editor,
* song/order editor,
* SID instrument editor,
* ADSR,
* waveform,
* pulse width,
* filter,
* sync/ring modulation,
* save/load native projects,
* stable realtime audio,
* performance profiling.

Version 0.2 should target GoatTracker compatibility.

Version 0.3 can introduce Perform automation and extended SID features.

Version 0.4+ can explore PCM drums, software synthesis and effects.

## 23. Long-Term Possibilities

After the core system works, investigate:

### SID file import

Run or emulate enough 6510/C64 playback logic to capture SID register writes,
then attempt to reconstruct editable tracker data.

Pipeline:

```
.SID
  ↓
6510 player
  ↓
SID register trace
  ↓
pattern/instrument analysis
  ↓
editable SID Tracker project
```

This should remain a separate research feature.

### Hardware SID output

Potentially allow patterns to drive external real SID hardware in addition to
software SID emulation.

### Commander X16 interoperability

Investigate a common SID composition representation that could also feed a
Commander X16 SID expansion project.

## Final Product Direction

The project should feel like:

> What if GoatTracker had been designed as a dedicated modern hardware instrument?

Not:

> Polyend Tracker with a SID plugin.

The strongest identity is a focused six-voice, dual-SID composition and
performance workstation with excellent SID programming ergonomics, GoatTracker
interoperability, and enough CPU headroom for carefully chosen modern
extensions.
