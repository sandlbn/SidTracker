# SIDTracker manual

SIDTracker turns the original Polyend Tracker into a Commodore 64 music
machine: two emulated SID sound chips, six voices, a pattern editor, an
instrument designer, and export to real C64 `.sid` files and GoatTracker.

This manual covers version 0.9. The full list of pattern commands and table
codes is in [commands.md](commands.md).

> SIDTracker is unofficial, experimental firmware, provided as is. Back up
> your SD card before installing. The authors are not responsible for lost
> data or damage. It is not made or supported by Polyend.

---

## 1. Install, update, go back

**Install**

1. Back up the SD card (copy everything to a computer).
2. Unzip `SIDTracker-<version>.zip` onto the root of the card. It adds the
   firmware to `/Firmware`, example songs, `.sid` files and the instrument
   library to `/SIDTracker`. Keep the stock image (`PolyendTracker_1.9.2.ptf`).
3. On the stock firmware: **Config > Firmware update**, choose
   `PolyendTracSIDTRv091.ptf`, confirm. The Tracker restarts into SIDTracker.

**Update** (from SIDTracker): copy the new `.ptf` to `/Firmware`, then
**Config**, go to **FIRMWARE UPDATE**, turn the jog to the image, press
**INSTALL**, then **INSTALL** again to confirm.

**Automatic install**: a text file `/Firmware/ot_restore.txt` whose first line
is an image name installs that image at the next power-on (then it is renamed
`ot_restore.done`). This also works when a firmware does not start.

**Back to stock**: install `PolyendTracker_1.9.2.ptf` the same way.

---

## 2. The idea in one page

- **Two SID chips, six tracks.** Tracks 1-3 play SID 1, tracks 4-6 play SID 2.
  Each track is one SID voice: one note at a time, with an oscillator
  (triangle, sawtooth, pulse, noise), an ADSR envelope, and the chip's filter.
- **Ticks.** Like a C64 music routine, the player runs 50 times a second (one
  *tick*). **Speed** is the number of ticks per pattern row (6 by default).
  **BPM 125** means 50 ticks a second (PAL); 150 means 60 (NTSC).
- **Patterns and the song.** A pattern has 6 tracks (and a drum column) of up
  to 128 rows. The **song** is the order of patterns.
- **Instruments are small programs.** An instrument sets the envelope and
  points into shared **tables** that run every tick: the *wave table*
  (waveform and pitch steps: arpeggios, drums), the *pulse table* (pulse-width
  sweeps) and the *filter table* (cutoff sweeps). You rarely need to touch the
  tables: the DESIGN page and the sound library write them for you.
- **Authentic or Extended.** An *Authentic* song uses only what a real C64
  can play, so it exports to `.sid` and GoatTracker unchanged. *Extended*
  adds the PCM drum column, effects, LFOs and the per-track mixer, which the
  exports cannot carry (the export check tells you what would be lost).

---

## 3. Keys

| Key | What it does |
|---|---|
| **Pattern** | pattern editor |
| **Instr. Params** | instrument editor; press again for the next page |
| **Performance** | live controls (and automation recording) |
| **Song** | order list, tempo, multispeed |
| **Master** | track mixer; press again for balance, effects and CPU |
| **Sample Loader** | Extended features: effects, drum kit, LFOs, pan |
| **Config** | settings, MIDI, firmware update |
| **File** | projects, GoatTracker files, `.sid` export, SID player |
| **Help** | key overview on screen (any button closes it) |
| **Play** | play the song from the current pattern; **Shift+Play** loops the pattern |
| **Rec** | edit mode (red marker); **Shift+Rec** undo, **Shift+Sample Rec** redo |
| **hold Instr** | instrument picker, on any screen |
| **Jog wheel** | changes the value under the cursor (Shift: bigger steps) |
| **The 8 buttons below the screen** | do what the label above each one says; it changes with the screen. This manual names them by their label, e.g. **KEEP** |
| **Pads** | a keyboard: bottom row = the current octave, each row up one octave |
| **Shift+Power** | screenshot to `/SIDTracker/Screenshots` |

When a question appears (delete, overwrite, export...), the bottom bar shows
just two buttons: the answer (e.g. **DELETE**) confirms, **CANCEL** (or any
other key) cancels.

---

## 4. Your first song

1. **Pattern**: press **Rec** (edit mode). Play notes on the pads: each one is
   written at the cursor and the cursor moves down. **OCT - / OCT +** change
   the octave. To give steps an instrument, press **Instr** (instrument
   column): each pad is an instrument, 1-48.
2. **Hold Instr**: the instrument picker opens. Press pads to hear each
   instrument, release Instr to choose one. Notes you enter now use it.
3. **Instr. Params**, then press it until **SOUNDS** (or Shift + the 7th button). The jog
   on CATEGORY picks BASS, LEAD and so on; press **Down** to PRESET and turn
   the jog: each preset loads into your instrument and plays at once.
   **KEEP** keeps it, **CANCEL** goes back.
4. **Play** listens; **Play** again stops.
5. **Song**: **Insert** adds a position (a copy of the current one); turn the
   jog to choose its pattern.
6. **File**, **SAVE NEW**: type a name on the pads, **OK**.

---

## 5. Pattern screen

Each track shows **note**, **instrument** and **command** (`C-4 05 A20`). The
`DR` column on the right is the PCM drum column (Extended).

| Key | Action |
|---|---|
| Up / Down | row (Shift: 16 rows) |
| Left / Right | field (note, instrument, command); Shift: previous / next track |
| Note / Instr / Fx1 | jump to the note / instrument / command field |
| Fx2 | key off (`===`) in edit mode |
| Pads | note field: notes. Instrument field: pad 1-48 = instrument 1-48 (as on the stock Tracker): in edit mode it is written into the step (an empty step also gets a note) and the cursor moves on; otherwise the pad just chooses the instrument. The step's instrument pad is lit. Command field in edit mode: the first 16 pads (lit red, **HEX PADS**) type hex digits |
| hold Instr | the instrument picker (all 63); release writes the choice into the step in edit mode |
| Jog | changes the field under the cursor in edit mode, otherwise scrolls |
| Delete | clears the field and moves on by STEP (on the instrument field: the whole step); Shift+Delete removes the cell of this track (the track's rows below move up) |
| Insert | inserts an empty cell on this track (the track's rows below move down) |
| Copy / Shift+Copy | copy / paste the track |
| PAT - / PAT + | previous / next pattern |
| OCT - / OCT + | octave down / up |
| INS - / INS + | previous / next instrument |
| STEP | rows the cursor moves after a note (0-8) |
| LEN | pattern length (16, 32, 48, 64, 96, 128; Rec first) |
| Shift + buttons 1-6 | mute / unmute track 1-6 (the bar shows MUTE 1-6 while Shift is held); button 7: the drum column |

Editing keys (notes, Delete, Insert, the jog) work in edit mode (Rec). While
playing with Rec off, the view follows the song and turns a page when the
play row reaches the bottom. The title bar shows the playing position, the tempo, the
instrument the pads play and its track (`INS 04 T1`), and `*` when the song
has unsaved changes.

**Commands** (third field): portamento, vibrato, set AD / SR / waveform,
table pointers, filter, `D` volume, tempo. The help line under the pattern
names the command under the cursor. The full table: [commands.md](commands.md).

> **`Dxx` is the SID chip's volume.** A real SID has one volume for its three
> voices, so `D05` on track 1 also lowers tracks 2 and 3. For a quieter
> track, use the Master mixer (section 9).

---

## 6. Song screen

The order list: which pattern plays at each position, with **transpose**
(semitones) and **repeat**.

| Key | Action |
|---|---|
| Up / Down | position (Shift: a page) |
| Left / Right | column: pattern, transpose, repeat |
| Jog | changes the value |
| Insert / Delete | duplicate / remove the position |
| RESTART | the song loops back to this position |
| BPM - / BPM + | tempo (Shift: 10) |
| SPD - / SPD + | speed (ticks per row) |
| AUTHENTIC / EXTENDED | song mode |
| EDIT PAT | open the pattern |
| MULTI | multispeed x1-x8 (Shift: down) |
| Play | play from the cursor |

**Multispeed** runs the player 2-8 times per frame: arpeggios, vibrato and
sweeps get faster and smoother. Speed and tempo commands count player calls
(as GoatTracker's `-S`): raise the speed with the multiplier to keep the row
pace. The examples HYPERARP (x4) and SILKSWEEP (x2) show it.

---

## 7. Instruments

### Choosing an instrument

**Hold Instr** on any screen: a grid of the 63 instruments, laid out like the
pads. Press a pad to hear a slot (the same note for every slot), turn the jog
or use the arrows to move, **49-63 >** for the second page. Release Instr to choose.
In edit mode on the Pattern screen, **Insert** (while holding Instr) also
writes the number into the cell. On the Instrument screen a tap on Instr
opens the picker until you press Instr again.

Every new instrument starts as the **init patch**: a sawtooth that sounds at
once.

### The Instrument screen

**Instr. Params** steps through the pages (Shift+Instr. Params goes back;
Shift + one of the first seven buttons jumps to one; the bar shows the page names while Shift is held): GENERAL, DESIGN, WAVE, PULSE, FILTER, TABLES,
SOUNDS. On every page **PREV INS / NEXT INS** choose the previous / next instrument, and
held pads play the instrument as you edit. **Shift+Left / Right** changes the
track the pads play on (it decides which voice a filter routes).

#### GENERAL

Name, ATTACK, DECAY, SUSTAIN, RELEASE (with the envelope graph), hard
restart / gate timer, first wave, vibrato and vibrato delay. Up / Down choose
a row, the jog changes it.

| Key | Action |
|---|---|
| Insert on NAME | type the name on the pads |
| INIT / Delete | back to the init patch (asks) |
| COPY / PASTE | copy an instrument, paste it into another slot |
| SAVE | save to the card: `/SIDTracker/Instruments/User/<name>.ins` |

The envelope rows never change the tables, so they are safe on any
instrument, including imported GoatTracker ones.

#### DESIGN: the sound designer

Synth controls that write the instrument's programs for you. Four pages
(**Up / Down**): **OSC / ENVELOPE** (waveform, attack hit, A D S R),
**PULSE** (width, PWM speed and range, hard restart), **FILTER** (type,
cutoff, resonance, sweep, sweep speed and length), **VIBRATO** (depth,
speed, delay). the buttons under the columns or **Left / Right** pick a column, the jog turns it.

Each control changes only its own part: the envelope only the instrument,
the filter only the filter program. The first waveform or pulse change on an
instrument the designer did not make turns it into a designer sound.

- **Insert**: a variation of the sound (small changes).
- **Shift+Insert**: a completely new random sound.

Both are *tried*: they play at once, **CANCEL** restores the instrument
exactly, **KEEP** keeps it.

#### WAVE, PULSE, FILTER, TABLES

The tables in hex, for GoatTracker-style editing. WAVE, PULSE and FILTER show
one table with the program that starts at the instrument's pointer; TABLES
shows all four side by side. The line above the help shows what uses the row
under the cursor; each header shows how many rows are used.

| Key | Action |
|---|---|
| Up / Down, Left / Right | row, left / right byte (TABLES: next table) |
| Jog / hex pads (Rec) | change the byte |
| Shift+jog | move the instrument's pointer (WAVE / PULSE / FILTER pages) |
| INS ROW / DEL ROW | insert / delete a row; every pointer, jump and pattern command that refers to the rows below follows |
| POINT HERE | the instrument's program starts at this row |
| USERS | what uses this row |
| PACK | remove rows nothing uses (asks; Shift+Rec undoes) |
| Delete | clear the row |

#### SOUNDS: presets, your library, arpeggiator

- **CATEGORY**: BASS, LEAD, CHORD, DRUM, PAD, FX, KEYS (49 presets), and
  **CARD**: the `.ins` files in `/SIDTracker/Instruments` and in its folders
  (one level deep; up to 512 files and 32 folders).
  The release puts the stock library there (every preset plus 16 *Classic*
  instruments); your own saves go to `User`.
- **PRESET**: turn the jog to try presets one after another; each one loads
  into the instrument and plays. **KEEP** (or Insert) keeps it, **CANCEL**
  restores what you had. Trying never fills the tables. Going to
  another page or instrument keeps the sound you were trying.
- **ARPEGGIATOR**: chord, order, speed and waveform; **Insert** writes the
  arpeggio into the instrument's wave program.

`.ins` files are GoatTracker instruments: they also load in GoatTracker, and
GoatTracker instruments copied into `/SIDTracker/Instruments` appear here.

---

## 8. Performance

Live control of the six voices, the two filters and the balance, layered on
top of the song.

- Rows: voice 1-6 (**MUTE**, **SOLO**, **WAVE** override, **PW** offset),
  filter 1-2 (**CUTOFF** offset, **RESONANCE**), balance.
- Up / Down row, Left / Right control, jog changes it, **Insert** toggles or
  resets.
- Pads: the top row mutes tracks 1-6, the second row solos them, the third
  steps a track's waveform override (song, triangle, saw, pulse, noise); the
  bottom row plays notes.
- **CLEAR**: all overrides off. **CLR AUTO**: remove this row's
  automation from the pattern (the playing one, or the edited one when
  stopped; asks).
- **Automation**: with **Rec** on while the song plays, changes to WAVE, PW,
  CUTOFF and RESONANCE are recorded into the pattern (Extended).

---

## 9. Master: the mixer

| Key | Action |
|---|---|
| hold TRK 1-6 + jog | level of that track |
| tap TRK 1-6 | mute; Shift+tap solo |
| DRUMS | drum column level (tap: mute) |
| hold MASTER + jog | master volume of the Tracker |
| Left / Right + jog | select a strip without holding |
| Delete | selected strip back to its default |
| Master again | page 2: balance, drum volume, effects, meters, CPU |
| Shift+Master | benchmark page (CPU, audio underruns, display faults) |

Each strip shows its level and a meter. Track levels are saved with the song
(Extended: not in the `.sid` or GoatTracker exports).

---

## 10. Extended features (Sample Loader key)

- **EFFECTS**: delay (mix, time in ticks, feedback), chorus, reverb.
  **FX ON / FX OFF** switches all effects.
- **DRUM KIT**: 16 PCM drum slots for the drum column. Your own WAV files
  go to `/SIDTracker/Kit` as `NNname.wav` (slot NN = 01-16); **RELOAD
  KIT** reads them.
- **MODULATION**: two LFOs (target, shape, period in ticks: larger is
  slower, depth) and a stereo pan per voice.

The drum column on the Pattern screen (`DR`) takes kit slots 1-16; with the
cursor there, the pads trigger slots.

---

## 11. Config

One line of help under the list explains the selected row.

- SID 1 / SID 2 model (6581 or 8580), clock (PAL / NTSC), stereo balance.
- **ONE SID ON BOTH SIDES**: a song that uses only SID 1 (tracks 4-6 empty),
  or a one-SID `.sid` tune in the SID player, also plays on SID 2, so it is
  heard on both sides and, over ASID, on both real chips. It switches itself
  off while tracks 4-6 have notes; the row says whether it is copying.
- Master volume and THEME (SIDTracker, Commodore 64, Green Screen, Amber).
- MIDI: input channel, clock out, sync in, ASID out (section 13).
- Song mode, hard-restart AD / SR, song NAME and AUTHOR (Insert: type on the
  pads).
- FIRMWARE UPDATE (section 1).

Unit settings (volume, theme, MIDI) are saved in `/SIDTracker/settings.txt`;
the others belong to the song.

---

## 12. File

| Key | Action |
|---|---|
| LOAD | load the selected project (asks if the song has unsaved changes) |
| SAVE | save under the current name |
| SAVE NEW | type a name on the pads (asks before overwriting) |
| NEW | empty song |
| GT FILES | GoatTracker files (`/SIDTracker/GoatTracker`) |
| EXPORT SID | `.sid` file for C64 players and real C64s |
| SID PLAYER | play `.sid` files from the card |
| DELETE | delete the selected project (asks) |

**`.sid` export.** EXPORT SID shows what will be exported (one or two SIDs, size,
memory range) and asks before writing to `/SIDTracker/SID`. The file uses
SIDTracker's own C64 player; a song with two SIDs puts the second at
`$D420`. Extended features are listed as not exported.

**GoatTracker.** In GT FILES: **IMPORT** a `.sng` (replaces the song) or
`.ins` (into the current instrument), **EXPORT SNG**, **EXPORT INS**.
GoatTracker has three channels: tracks 4-6 are not exported, and the check
lists anything else that changes.

**SID player.** Plays PSID files (one or two SIDs, subtunes) from anywhere on
the card, for example the HVSC collection. **Up / Down** choose (Shift: 16),
**PLAY** (or Insert) plays a file or opens a folder, **STOP**, **SONG - /
SONG +** (or Left / Right) previous / next subtune, **UP** a folder.

---

## 13. MIDI, ASID and real SID chips

Config has four MIDI rows (DIN and USB):

- **MIDI IN**: notes play the pads' instrument on the cursor track (and are
  entered in edit mode); CC 74 cutoff, CC 71 resonance.
- **MIDI CLOCK OUT**: clock (6 per row) with Start / Stop.
- **MIDI SYNC IN**: follow an external clock (tempo, Start, Stop, Continue).
- **ASID OUT**: every SID register change as ASID messages, for real chips
  such as a USBSID-Pico or other ASID players.

**Real SID chips through a computer**: connect the Tracker and the
USBSID-Pico to a Linux computer, run `tools/sidlink/pc_bridge.sh`, then set
ASID OUT off and back to USB so the receiver gets the start message.
**Without a computer**: the SIDLink bridge on a Raspberry Pi Pico 2 with a
powered USB hub (`tools/sidlink/README.md`).

---

## 14. Undo, help, screenshots

- **Shift+Rec** undoes the last edit, **Shift+Sample Rec** redoes. Edits made
  within a fraction of a second count as one step; more than 64 steps are
  kept. Loading or starting a song clears the history.
- **Help** shows the key overview; any button closes it.
- **Shift+Power** saves the screen as a BMP in `/SIDTracker/Screenshots`.

---

## 15. Folders on the card

| Folder | Contents |
|---|---|
| `/Firmware` | firmware images and `ot_restore.txt` |
| `/SIDTracker/Projects` | your songs (`.sidt`) and the examples |
| `/SIDTracker/Instruments` | instrument library (`.ins`): stock folders and `User` |
| `/SIDTracker/GoatTracker` | `.sng` / `.ins` to import, GoatTracker exports |
| `/SIDTracker/SID` | `.sid` exports and example `.sid` files |
| `/SIDTracker/Kit` | your drum samples (`NNname.wav`) |
| `/SIDTracker/Screenshots` | screenshots |
| `/SIDTracker/settings.txt` | unit settings |
| `/SIDTracker/autosave.sidt` | the workspace, restored at power-on |

---

## 16. Troubleshooting

- **A note is silent.** Check the instrument (hold Instr and play its slot).
  A slot from a song made before 0.8 that was never set up stays silent on
  purpose, so old songs sound as they did: load a preset or press INIT on
  GENERAL.
- **`D` command changes other tracks.** It is the chip volume (section 5);
  use the Master mixer for one track.
- **The screen flickers.** Open the benchmark page (Shift+Master): "Display
  faults" counts lost frames. Note which screen it happens on and report it.
- **No sound from a real SID over ASID.** The receiver needs the start
  message: switch ASID OUT off and on after connecting.
- **A table is full.** Instrument > table page **PACK** removes rows
  nothing uses.
- **Something went wrong.** **Shift+Rec** undoes. The workspace is saved
  automatically and comes back at power-on.
