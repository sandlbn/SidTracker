#!/usr/bin/env python3
"""bg2text.py -- Blast Grid 64 songs -> SIDTracker song text (tools/text2sidt).

Reads the game's own song sources (data/music/*.txt) with its own parsers
(tools/mk_music.py, sid/tools/sidlib.py, tools/mk_psgmusic.py), imported
read-only: nothing in the game repository is written (no bytecode either).
The C64 version's arrangement (sid/data/sid_arrange.txt: which channel on
which of the six SID voices) and SID instruments / drum programs / drum map
(sid/data/sid_instruments.txt) are used as they are, so the result sounds
like the game's dual-SID music.

    bg2text.py [--game DIR] OUTDIR        one OUTDIR/<SONG>.txt per song

Mapping (docs/blastgrid-import.md): a section is a pattern, the order list
keeps the section transposes and the loop point, voices 0-5 are tracks 1-6,
`D4` notes, `.` key off, `^D4` a tie (3 00), `D4:037` a fast arpeggio (an
arp variant of the instrument), `@inst` the instrument column, the groove
the speed (two values: funk tempo), the 59.52 Hz tick 149 bpm.
"""
import math
import os
import re
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def gt_freq_table():
    src = open(os.path.join(ROOT, "src/tracker/playback/player.cpp")).read()
    body = src[src.index("kGtFreq"):]
    body = body[:body.index("};")]
    return [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4})", body)]


FREQ = gt_freq_table()


def nearest_semitone(word):
    best, bd = 0, 1e9
    for i, f in enumerate(FREQ):
        d = abs(math.log(max(word, 1) / f))
        if d < bd:
            best, bd = i, d
    return best


class Tables:
    """Wave / pulse / filter / speed tables, filled in order; identical
    programs are shared.  Jumps in a program are written relative (1 = its
    first row) and relocated here."""

    def __init__(self):
        self.rows = {"wave": [], "pulse": [], "filter": [], "speed": []}

    def add(self, kind, prog):
        t = self.rows[kind]
        n = len(prog)

        def reloc(r, start):
            return (r[0], start + r[1] - 1) if r[0] == 0xFF and r[1] else r

        for s in range(1, len(t) - n + 2):
            if all(t[s - 1 + i] == reloc(prog[i], s) for i in range(n)):
                return s
        start = len(t) + 1
        if start + n - 1 > 255:
            raise SystemExit(f"{kind} table full")
        t.extend(reloc(r, start) for r in prog)
        return start


def delay_rows(ticks, keep_row):
    """Rows that wait `ticks` ticks (each delay row: L ticks + 1)."""
    out = []
    while ticks > 0:
        n = min(15, ticks - 1) if ticks > 1 else 0
        if n == 0:
            out.append(keep_row)
            ticks -= 1
        else:
            out.append((n, 0x80))
            ticks -= n + 1
    return out


class Builder:
    def __init__(self, insts, progs):
        self.sid_insts = insts
        self.progs = progs
        self.tables = Tables()
        self.slots = {}          # (name, arp offsets, arp speed, drum note) -> slot
        self.lines = []

    def pulse_prog(self, inst):
        pw = inst.pw & 0xFFF
        prog = [(0x80 | (pw >> 8), pw & 0xFF)]
        s = inst.pws
        if not s:
            return prog + [(0xFF, 0)]
        lo, hi = 0x100, 0xEFF
        first = (hi - pw) // abs(s) if s > 0 else (pw - lo) // abs(s)
        full = (hi - lo) // abs(s)

        def run(ticks, spd):
            out = []
            while ticks > 0:
                k = min(127, ticks)
                out.append((k, spd & 0xFF))
                ticks -= k
            return out
        body = run(max(first, 1), s)
        loop = len(prog) + len(body) + 1
        body += run(full, -s) + run(full, s)
        return prog + body + [(0xFF, loop)]

    def vib_row(self, inst):
        if not inst.vib:
            return 0, 0
        d, r, n = inst.vib
        spd = max(0, round(128 / max(r, 1)) - 2)
        shift = {1: 5, 2: 4, 3: 3, 4: 2}.get(n, 4)
        return self.tables.add("speed", [(0x80 | min(spd, 0x7F), shift)]), max(d, 1)

    def slot(self, name, arp=None, arp_speed=1, drum_note=None):
        key = (name, arp, arp_speed, drum_note)
        if key in self.slots:
            return self.slots[key]
        if len(self.slots) >= 63:
            raise SystemExit("more than 63 instruments")
        inst = self.sid_insts[name]
        ctrl = (inst.ctrl & 0xFE) | 1
        wave = []
        if inst.prog:
            for c, fhi in self.progs[inst.prog]:
                semi = drum_note if not fhi else nearest_semitone(fhi << 8)
                wave.append(((c & 0xFE) | 1, 0x80 + max(semi, 1)))
            steps = len(wave)
            last = wave[-1]
            if inst.len and inst.len > steps:
                wave += delay_rows(inst.len - steps, last)
            if inst.len:
                wave.append((last[0] & 0xFE, 0x80))
            wave.append((0xFF, 0))
        elif arp:
            for off in arp:
                wave += [(ctrl, off)] * arp_speed
            wave.append((0xFF, 1))
        else:
            wave.append((ctrl, 0x00))
            if inst.len:
                wave += delay_rows(inst.len - 1, (ctrl, 0x80))
                wave.append((ctrl & 0xFE, 0x80))
            wave.append((0xFF, 0))
        wp = self.tables.add("wave", wave)
        pp = self.tables.add("pulse", self.pulse_prog(inst))
        vp, vd = self.vib_row(inst)
        ad = inst.chip_ad()
        n = len(self.slots) + 1
        self.slots[key] = n
        label = name.upper()
        if arp:
            label += " ARP " + "".join("%X" % o for o in arp)
        gate = 3 if inst.hr else 0x83
        first = (wave[0][0] if inst.prog else ctrl)
        self.lines.append("inst %d %02X %02X %02X %02X %02X %02X %02X %02X %02X %s" % (
            n, ad, inst.sr, first, wp, pp, 0, vp, vd, gate, label[:24]))
        return n


def song_text(name, src, arr, builder, drummap, kits, ym):
    out = []
    out.append(f"song BLAST GRID {name.replace('_', ' ')}"[:24])
    out.append("author Marcin Spoczynski")             # as in the game's .sid
    out.append("bpm 149")                            # 59.6 Hz (game: 59.52 Hz)
    groove = src.groove
    out.append(f"speed {groove[0]}")
    out.append("hr 00 00")
    out.append("model 8580 8580")
    out.append("balance 60")
    voices = {sv: ch for sv, ch in arr.voices.items()}
    chans = src.chans
    kit = kits.get(src.kit) if src.kit else None
    drum_prio = {k: v for k, v in drummap.items()}

    def arp_speed(c):
        return 2 * ((c.arp + 1) // 2)

    note_re = re.compile(r"^(\^)?([A-G][#b]?-?\d)(?::([0-9a-z]+))?$")
    sec_names = list(src.sections)
    pat_of = {s: i for i, s in enumerate(sec_names)}
    cells = []

    def channel_tokens(content, cname):
        c = chans[cname]
        if c.echo:
            srcname, delay = c.echo
            toks = content.get(srcname, [])
            toks = [t for t in toks if not t.startswith("@")]
            return ["."] * delay + toks
        return content.get(cname, [])

    for sname in sec_names:
        rows, content = src.sections[sname]
        p = pat_of[sname]
        out.append(f"pattern {p} {rows}")
        for sv in range(6):
            if sv not in voices:
                continue
            cname, override = voices[sv]
            if cname == "drums":
                toks = content.get("drums", [])
                for r, tok in enumerate(toks[:rows]):
                    if tok in (".", "-") or not kit:
                        continue
                    best = None
                    for key in tok:
                        for _slot, pinst, note in kit.get(key, []):
                            sname2, prio = drum_prio.get(pinst, (None, 0))
                            if sname2 and (best is None or prio > best[0]):
                                best = (prio, sname2, note)
                    if best:
                        semi = best[2] - 12
                        slot = builder.slot(best[1], drum_note=semi)
                        cells.append((p, r, sv, 49, slot, 0, 0))   # any note: drums play fixed pitches
                continue
            c = chans[cname]
            default = override or c.inst
            if c.echo and override:
                default = override
            cur = None
            toks = channel_tokens(content, cname)
            pend = None
            r = 0
            first = True
            for tok in toks:
                if tok.startswith("@"):
                    pend = tok[1:]
                    continue
                if r >= rows:
                    break
                if tok == "-":
                    if first:
                        cells.append((p, r, sv, 0xFE, 0, 0, 0))   # section opens without a note: key off
                elif tok == ".":
                    cells.append((p, r, sv, 0xFE, 0, 0, 0))
                else:
                    m = note_re.match(tok)
                    if not m:
                        raise SystemExit(f"{name}: bad token {tok!r}")
                    legato, nn, arp = m.group(1), m.group(2), m.group(3)
                    semi = ym.note_midi(nn) - 12
                    iname = pend or default
                    if pend is None and override and c.inst == default:
                        iname = override
                    if iname not in builder.sid_insts:
                        iname = default
                    offs = tuple(int(ch, 36) for ch in arp) if arp else None
                    slot = builder.slot(iname, offs, arp_speed(c)) if offs else builder.slot(iname)
                    note = max(1, min(96, semi + 1))
                    if legato and not first:
                        cells.append((p, r, sv, note, 0, 3, 0))
                    else:
                        cells.append((p, r, sv, note, slot if slot != cur else slot, 0, 0))
                    cur = slot
                pend = None
                first = False
                r += 1
    # Song order (sections with transposes), loop point, or an end.
    order = [(pat_of[s], tr) for s, tr in src.order]
    restart = src.loop if src.loop is not None else None
    if restart is None:
        end = len(sec_names)
        out.append(f"pattern {end} 16")
        for sv in voices:
            cells.append((end, 0, sv, 0xFE, 0, 0, 0))
        order.append((end, 0))
        restart = len(order) - 1
    if len(groove) > 1:
        # Funk tempo: speed-table row (first, second), started on row 0 of the
        # first pattern on a track without a command there.
        row = builder.tables.add("speed", [(groove[0], groove[1])])
        p0 = order[0][0]
        used = {sv for (p, r, sv, *_x) in cells if p == p0 and r == 0 and _x[2]}
        free = next(sv for sv in range(6) if sv not in used)
        cells.append((p0, 0, free, 0, 0, 0xE, row))
    for p, tr in order:
        out.append(f"order {p} {tr} 1")
    out.append(f"restart {restart}")
    merged = {}
    for p, r, sv, note, inst, cmd, par in cells:
        k = (p, r, sv)
        o = merged.get(k, (0, 0, 0, 0))
        merged[k] = (note or o[0], inst or o[1], cmd or o[2], par if cmd else o[3])
    for (p, r, sv), (note, inst, cmd, par) in sorted(merged.items()):
        out.append(f"cell {p} {r} {sv} {note:02X} {inst:02X} {cmd:X} {par:02X}")
    return out


def main(argv):
    game = os.path.expanduser("~/Projects/BlastGridC64")
    if "--game" in argv:
        game = argv[argv.index("--game") + 1]
    outdir = argv[-1]
    sys.path.insert(0, os.path.join(game, "tools"))
    sys.path.insert(0, os.path.join(game, "sid", "tools"))
    import mk_music as ym          # noqa: E402  (read-only)
    import mk_psgmusic as pm       # noqa: E402
    import sidlib as sl            # noqa: E402
    insts, progs, drummap = sl.parse_instruments()
    arr = sl.parse_arrangement()
    _, pkits = pm.parse_psg_instruments(os.path.join(sl.MUSIC_SRC, "psg_instruments.txt"))
    _, ykits = ym.parse_instruments(os.path.join(sl.MUSIC_SRC, "instruments.txt"))
    os.makedirs(outdir, exist_ok=True)
    for name, fn in sl.read_manifest():
        if name == "TESTSCALE":
            continue
        src = ym.parse_song(os.path.join(sl.MUSIC_SRC, fn), ykits)
        b = Builder(insts, progs)
        body = song_text(name, src, arr[name], b, drummap, pkits, ym)
        lines = body[:7] + b.lines
        for kind in ("wave", "pulse", "filter", "speed"):
            for i, (l, r) in enumerate(b.tables.rows[kind], 1):
                lines.append(f"table {kind} {i} {l:02X} {r:02X}")
        lines += body[7:]
        path = os.path.join(outdir, name + ".txt")
        with open(path, "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{path}: {len(b.slots)} instruments, "
              + ", ".join(f"{k} {len(v)}" for k, v in b.tables.rows.items()))


if __name__ == "__main__":
    main(sys.argv[1:])
