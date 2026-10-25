SIDTracker @VERSION@ -- dual-SID tracker firmware for the original Polyend Tracker
================================================================================

DISCLAIMER: USE AT YOUR OWN RISK. SIDTracker is experimental, unofficial
firmware, provided "as is" without warranty of any kind (LICENSE.txt). The
authors are not responsible for any loss of data, damage to your Tracker or
SD card, or any other harm caused by installing or using it. Back up your
SD card first. Installing third-party firmware may affect your warranty.
SIDTracker is an independent project, not affiliated with, endorsed by or
supported by Polyend. The way back to stock is described below.

Contents (unzip onto the root of the Tracker's SD card)
-------------------------------------------------------
  Firmware/@IMAGE@        the firmware
  SIDTracker/Projects/*.sidt   example songs (File > LOAD)
  SIDTracker/SID/*.sid         the same songs exported as C64 .sid files
                               (play them in any SID player)
  SIDTracker/Instruments/      instrument library (.ins): Instrument >
                               SOUNDS > CARD; also loads in GoatTracker
  MANUAL.md                    the manual: start here (any text viewer)

Install
-------
1. Copy the folders onto the SD card. Keep the stock firmware image
   (PolyendTracker_1.9.2.ptf) in /Firmware as well.
2. On the stock firmware: Config > Firmware update > @IMAGE@.

Back to stock
-------------
- In SIDTracker: Config, F7 FIRMWARE, turn the jog wheel to
  PolyendTracker_1.9.2.ptf, press F8 INSTALL twice.
- If SIDTracker does not start: create /Firmware/ot_restore.txt containing
  the single line  PolyendTracker_1.9.2.ptf  and power on.

Example songs (original music, MIT licence like the firmware)
--------------------------------------------------------------
  DUALDEMO     both SIDs: bass, filtered arps, lead, drums, pad, echo
  NIGHTDRIVE   one SID (6581): drums woven into the bass track
  PCMGROOVE    both SIDs + the PCM drum column, delay, reverb, LFO, pan
               (its .sid has the SID parts only)
  FILTERS      both SIDs (8580): slow resonant filter sweeps, 100 bpm
  CHIPMARCH    one SID: swing tempo, slides, portamento, vibrato

New in 0.7
----------
- File > F7 SID PLAYER: plays C64 .sid files (PSID, one or two SIDs,
  subtunes) from anywhere on the card, e.g. the High Voltage SID Collection.
- Instrument > F2 DESIGN: shape a sound with synth controls.
- Master: mixer with level meters; Config F7 / F8: firmware update.
- Instrument > F7 SOUNDS: 49 ready instruments (bass, lead, chord, drum,
  pad, fx, keys) and an arpeggiator that writes chord programs.
- File > F6: export the song as a C64 .sid file (/SIDTracker/SID).
- Config > THEME: SIDTracker, Commodore 64, Green Screen, Amber
  (remembered, with the master volume, in /SIDTracker/settings.txt).
- Master: mixer with level meters (SID1, SID2, drums, output) and the CPU
  load; Shift+Master opens the benchmark page.
- Shift+Power: screenshot, saved as /SIDTracker/Screenshots/SHOTnnnn.bmp.
- Power button like stock: once switched off, the Tracker stays off when
  USB power comes back and starts when you press the button.

Blast Grid 64 soundtrack (SIDTracker/Projects/BG_*.sidt, .sid files in
SIDTracker/SID): the 13 songs of the game Blast Grid 64, music by Marcin
Spoczynski, imported from the game with its dual-SID arrangement.

Keys
----
Pattern / Instr. Params / Performance / Song: the main screens
(Instrument: F1-F7 pages, F8 next instrument, Shift+F8 previous). Sample
Loader: effects, drum kit, LFOs. Config: settings and firmware update.
File: load / save, GoatTracker (F5), export .sid (F6). Master: mixer.
Help: key help. Rec: edit mode. Play: play / stop.

Source, documentation, licence: see the project repository (MIT).
