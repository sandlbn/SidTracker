# Pattern commands, instruments and tables

How SIDTracker plays a song: the semantics of
`src/tracker/playback/player.cpp`, tested in `tests/player_test.cpp`.

They are GoatTracker 2's (PLAN.md §8: compatibility first). Where
GoatTracker's documentation is silent, its behaviour was measured on songs
packed by GoatTracker's own tools; songs then play the same in both.
`docs/goattracker-format.md` has the evidence and the few known
differences.

## Timing

- A **tick** is one tracker frame. The tick rate is `BPM × 2 / 5` Hz: 125 BPM = 50 Hz (PAL frame, the default), 150 BPM = 60 Hz (NTSC). Other values cannot be exported to GoatTracker.
- Ticks are counted in **audio samples** in the audio callback (`Engine::fill`), never in UI frames: 882 samples at 44.1 kHz and 125 BPM.
- **Speed** = ticks per row (song setting, `Fxx`, `Exx`). Register writes happen on tick boundaries.
- Per row and track:

| Tick | What happens |
|---|---|
| `speed − gate timer` (fetch tick) | the next row is read; a new note there gets gate off + hard-restart ADSR (see Instrument); a key off takes effect now; no pulse-table step |
| 0 | instrument change, note init (first-frame wave, ADSR, table pointers), one-shot commands 5..F; no realtime commands; no pulse step if this is the last row before a new pattern |
| every tick | wave-table step; if it set no pitch: the running command 1..4 or the instrument vibrato |

- The filter tables and the filter / volume registers are handled before the tracks, so a filter pointer or a B, C, D command takes effect on the next tick.
- Tracks 1-3 drive SID1 voices 1-3, tracks 4-6 SID2 voices 1-3. Filter state belongs to the track's chip.

## Notes

| Value | Shown | Meaning |
|---|---|---|
| 0 | `---` | nothing |
| 1-96 | `C-0`..`B-7` | note: initialise the instrument (with command 3: tone portamento instead) |
| 0xFD | `+++` | gate mask on (key on) |
| 0xFE | `===` | gate mask off (key off), effective at the fetch tick |

The gate bit written to the SID is the **wave value's gate bit AND the gate
mask**: a wave-table entry without bit 0 releases the note even while the
key is held. The order entry's transpose is added to the notes of its
pattern.

## Commands

One hex digit plus a data byte: `C-4 01 A03`.

| Cmd | Name | Data | Notes |
|---|---|---|---|
| 0 | none | – | ends a running 1..4 |
| 1 | portamento up | speed table row (16-bit speed per tick) | runs until a row without 1..4 |
| 2 | portamento down | speed table row | |
| 3 | tone portamento | speed table row; `00` = tie (pitch jumps on tick 1) | towards the row's note without retrigger |
| 4 | vibrato | speed table row (left: speed, right: depth), `00` off | replaces the instrument vibrato |
| 5 | set AD | value | tick 0 |
| 6 | set SR | value | tick 0 |
| 7 | set waveform | control value | tick 0; a running wave table may overwrite it |
| 8 | wave table pointer | row, `00` stops | tick 0 |
| 9 | pulse table pointer | row, `00` stops | |
| A | filter table pointer | row, `00` stops | this track's chip |
| B | filter control | resonance << 4 \| voice bits; `00` = filter off and filter table stopped | |
| C | cutoff | high byte | |
| D | volume | `0Y` sets the volume; `X` ≠ 0 is a GoatTracker timing mark (ignored) | |
| E | funk tempo | speed table row: left and right speeds alternate per row | |
| F | tempo | `02`..`7F` ticks per row; `00`/`01` recall the funk tempo; `83`..`FF` set one GoatTracker channel's tempo, applied to all tracks here | |

The one-shot commands 5..F leave a running 1..4 alone; a row with command
0 ends it.

### Vibrato

Each step moves the pitch by `depth`. The direction changes every `speed + 2`
steps, and the first half cycle (up) is half as long. Steps count only on
ticks where the pitch moves. The instrument vibrato starts on the
`vibrato delay`-th tick on which the wave table left the pitch alone; delay
and phase restart while a command 1..3 runs.

### Speed table

16-bit values (`left << 8 | right`) for portamento; vibrato reads left =
speed and right = depth; funk tempo reads two tempos. Left byte bit 7:
"note-independent" speed (right = right shift of the semitone step;
formula unverified).

## Instrument

| Field | Meaning |
|---|---|
| AD, SR | ADSR at note init |
| first wave | control value on the init tick: `01`..`FD` written (gate bit through the mask), `FE` gate off / `FF` gate on with the waveform unchanged, `00` neither changed |
| wave / pulse / filter pointer | table rows started at note init; pulse and filter `00` leave a running table alone, wave `00` stops it |
| vibrato | speed table row (`00` off) |
| vibrato delay | `00` = no instrument vibrato, else ticks as described above |
| HR / gate timer | bits 0..5: ticks before the next row at which it is fetched; `$80` = no hard restart (gate off only), `$40` = no gate off either (legato); the new note's instrument decides |

The hard-restart ADSR is a song setting (Settings, default `0F00`, as in
GoatTracker).

## Tables

Four shared tables of 255 rows, each row a left and a right byte; row 0 =
"no pointer". Each tick a running table executes one row (jumps are followed
at once, at most 8 per tick). `FF xx` = jump to row `xx`, `FF 00` = stop.

### Wave table (per track)

| Left | Meaning |
|---|---|
| `00` | waveform unchanged |
| `01`-`0F` | wait that many ticks, then apply the right byte on the next tick |
| `10`-`DF` | control value (waveform, ring, sync, test, gate) |
| `E0`-`EF` | control value `00`-`0F` (inaudible waveforms) |
| `F0`-`FE` | command 0..E with the right byte as data: 5..E as one-shots, 1..4 one step now; no other pitch change this tick |

| Right | Meaning |
|---|---|
| `00`-`5F` | note + 0..95 semitones |
| `60`-`7F` | note − 32..−1 |
| `80` | pitch unchanged (realtime commands and vibrato may run) |
| `81`-`DF` | absolute note C#0..B-7 |

Example arpeggio: `41 00`, `00 04`, `00 07`, `00 0C`, `FF 01`.

### Pulse table (per track)

| Left | Meaning |
|---|---|
| `80`-`FE` | pulse width `(left & 0F) << 8 \| right` |
| `01`-`7F` | for `left` ticks add the signed `right` (12-bit wrap); `00` = 256 ticks |

### Filter table (per chip)

| Left | Meaning |
|---|---|
| `80`-`F0` | passband `left & 70` (`90` low, `A0` band, `C0` high pass), right = resonance / voice bits; a following `00 xx` row runs on the same tick |
| `00` | cutoff high byte = right |
| `01`-`7F` | for `left` ticks add the signed `right` to the cutoff |

## Sound designer (Instrument > F2 DESIGN)

The instrument as synth controls: WAVEFORM (triangle, saw, pulse, noise and
their combinations), ATTACK HIT (none, noise click, octave or fifth blip on
the first tick), ATTACK / DECAY / SUSTAIN / RELEASE, PULSE WIDTH, PWM SPEED
and PWM RANGE (a looping width sweep), HARD RESTART, FILTER (off, low /
band / high pass, notch) with CUTOFF, RESONANCE and SWEEP (off, down, up,
wobble; speed and length), VIBRATO depth / speed / delay. Up/Down rows,
Left/Right columns, jog wheel changes (Shift: coarse). Graphs show the
envelope, the pulse width and a sketch of the filter response.

Every change rewrites the instrument's wave, pulse and filter programs and
replays the held pads, so you hear it at once. The programs have fixed
shapes (`tracker/song/designer.h`): they are rewritten in place while no
other instrument points into them, so turning a knob never fills the tables;
shared rows are left alone and the instrument gets its own copy. Opening the
page reads a designed instrument back from its tables. The first change to
an instrument made elsewhere replaces its programs (the page says so).

## Sound library and arpeggiator (Instrument > F7 SOUNDS)

**Library.** 49 presets of SIDTracker's own, in seven categories: BASS,
LEAD, CHORD, DRUM, PAD, FX, KEYS. They use the classic C64 techniques:
hard-restart basses, chord arpeggios in the wave table, noise + pulse drums
at fixed pitches, pulse width and filter sweeps, ring-modulated bells. The
right side lists the programs a preset will write. Insert on the
CATEGORY / PRESET rows loads the preset into the current instrument (a
second push when the slot is in use). Its table rows go after the rows the
song already uses, and identical rows already there are reused. A preset's
filter routes the voice of the track under the Pattern cursor. "Table full"
leaves the song unchanged.

**Arpeggiator.** CHORD (major, minor, 7th, maj7, min7, sus2, sus4, dim, aug,
power, octave, 6th, min9), ORDER (up, down, up-down, up + octave), SPEED (1-4
ticks per note), WAVEFORM (pulse, saw, triangle, saw+tri). Insert on these
rows writes a looping wave program and points the current instrument at it;
the rest of the instrument is kept. A pulse arp on an instrument without a
pulse program also gets a pulse width ($800).

F8 selects the next instrument, Shift+F8 the previous one.

## Perform overrides and automation

Live controls on the Perform page sit on top of the song:
- a waveform override per voice (sync, ring and test bits are kept)
- a pulse-width offset per voice
- a cutoff offset and resonance override per chip
- mute and solo (the voice keeps running for sync and ring modulation)
- SID balance

**Automation (M7).** With Rec on while the song plays, changes to WAVE, PW,
CUT and RES are recorded into the playing pattern: the value is stored on
the current row, and while a control keeps being turned (within 0.7 s) on
every following row too. A recorded row sets its lanes on tick 0 when it
plays; a live change wins until the next recorded row. When a song starts,
the lanes it automates go back to neutral. 16 lanes per row: waveform and
pulse width per voice, cutoff and resonance per chip. Mute, solo and
balance are not recorded. Automation is saved in the project (`AUTO`) and
is not exported to GoatTracker (the export check says so).

## Extended engine (M8)

Not part of GoatTracker: the export check lists each one in use, and a
GoatTracker export leaves them out. The Sample Loader key opens the
EXTENDED screen (F1-F3 pages, jog wheel edits, F8 turns all effects off or
back on; the switch is not saved in the song).

**Drum column.** Right of track 6 every pattern has a PCM drum column, one
kit slot (1-16) per row; the row triggers it on tick 0. With Rec on, pads
1-16 enter a slot at the cursor (otherwise they audition it), Delete clears
it, Insert / Delete (Shift) / Copy work as on a track. Mute and solo treat
the column as a seventh track. Four voices; slot 4 (closed hat) chokes
slot 5 (open hat).

**Kit.** Slots 1-8 are synthesised at boot: KICK, SNARE, CLAP, HAT CLOSED,
HAT OPEN, TOM, RIM, CRASH. Any slot can be replaced by a WAV file in
`/SIDTracker/Kit/` named `NNname.wav` (NN = 01-16; PCM 8 or 16 bit, mono
or stereo, 4 s at most per slot). They are loaded at boot and with F5 on the
DRUM KIT page. Drum volume 0-100 per song.

**Effects** (master bus, in this order; mix 0 = off and costs nothing):
| Effect | Parameters |
|---|---|
| Delay | mix, time in ticks (1-64, follows the tempo), feedback (ping-pong) |
| Chorus | mix, rate (0.05-5 Hz), depth |
| Reverb | mix, size, damping |

**Modulation.** Two LFOs, each with a target (PW of a track 1-6, cutoff of
SID1 / SID2), shape (triangle, sine, square, saw, random), period in ticks
and depth; the offset is added to what the song writes, every tick.
Per-voice pan (-100 left to +100 right, 0 = centre via the SID balance).
