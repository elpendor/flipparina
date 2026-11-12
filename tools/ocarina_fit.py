"""Fit a MIDI melody to the ocarina range.

usage: python ocarina_fit.py in.mid out.mid [--channel N] [--allow-bend] [--transpose N]
"""
import argparse
import struct

BUTTONS = {61, 62, 63, 64, 65, 66, 68, 69, 70, 71, 72, 73, 74, 75}
BENDS = {59, 60, 67, 76, 77}


def varlen_read(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def varlen_write(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def read_midi(path):
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        raise SystemExit("not a MIDI file")
    hl = struct.unpack(">I", d[4:8])[0]
    division = struct.unpack(">H", d[12:14])[0]
    pos = 8 + hl
    notes, tempos = [], []
    while pos + 8 <= len(d):
        cid, ln = d[pos:pos + 4], struct.unpack(">I", d[pos + 4:pos + 8])[0]
        start, end = pos + 8, min(pos + 8 + ln, len(d))
        pos = end
        if cid != b"MTrk":
            continue
        i, tick, run, active = start, 0, 0, {}
        while i < end:
            dt, i = varlen_read(d, i)
            tick += dt
            b = d[i]
            if b == 0xFF:
                t = d[i + 1]
                ln2, i = varlen_read(d, i + 2)
                if t == 0x51:
                    tempos.append((tick, d[i:i + ln2]))
                i += ln2
                if t == 0x2F:
                    break
                continue
            if b in (0xF0, 0xF7):
                ln2, i = varlen_read(d, i + 1)
                i += ln2
                continue
            if b & 0x80:
                run = b
                i += 1
            st, ch = run & 0xF0, run & 0x0F
            n = 1 if st in (0xC0, 0xD0) else 2
            p, v = d[i], (d[i + 1] if n == 2 else 0)
            i += n
            on = st == 0x90 and v > 0
            off = st == 0x80 or (st == 0x90 and v == 0)
            if (on or off) and (ch, p) in active:
                s = active.pop((ch, p))
                if tick > s:
                    notes.append([s, tick, ch, p])
            if on:
                active[(ch, p)] = tick
    return division, tempos, notes


def fold(p, allowed):
    for k in range(0, 6):
        for sign in (1, -1):
            q = p + sign * 12 * k
            if q in allowed:
                return q, k
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--channel", type=int)
    ap.add_argument("--allow-bend", action="store_true")
    ap.add_argument("--transpose", type=int)
    args = ap.parse_args()

    division, tempos, notes = read_midi(args.src)
    notes = [n for n in notes if n[2] != 9]
    if args.channel:
        ch = args.channel - 1
    else:
        counts = {}
        for n in notes:
            counts[n[2]] = counts.get(n[2], 0) + 1
        ch = max(counts, key=counts.get)
    mel = sorted((n for n in notes if n[2] == ch), key=lambda n: (n[0], -n[3]))

    mono = []
    for n in mel:
        if mono and mono[-1][0] == n[0]:
            continue
        if mono and mono[-1][1] > n[0]:
            mono[-1][1] = n[0]
        mono.append(n)

    allowed = BUTTONS | (BENDS if args.allow_bend else set())
    best = None
    for t in [args.transpose] if args.transpose is not None else range(-36, 37):
        moves = 0
        ok = True
        for n in mono:
            q, k = fold(n[3] + t, allowed)
            if q is None:
                ok = False
                break
            moves += k
        if ok:
            score = (moves, abs(t))
            if best is None or score < best[0]:
                best = (score, t)
    if best is None:
        raise SystemExit("could not fit melody")
    t = best[1]
    out, moved = [], 0
    for s, e, _, p in mono:
        q, k = fold(p + t, allowed)
        moved += k > 0
        out.append((s, e, q))

    events = []
    for tick, data in tempos[:1] or [(0, (500000).to_bytes(3, "big"))]:
        events.append((tick, 0, b"\xff\x51\x03" + data))
    for s, e, p in out:
        events.append((e, 1, bytes([0x80, p, 0])))
        events.append((s, 2, bytes([0x90, p, 100])))
    for tick, data in tempos[1:]:
        events.append((tick, 0, b"\xff\x51\x03" + data))
    events.sort(key=lambda x: (x[0], x[1]))
    trk, last = b"", 0
    for tick, _, data in events:
        trk += varlen_write(tick - last) + data
        last = tick
    trk += b"\x00\xff\x2f\x00"
    with open(args.dst, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, division))
        f.write(b"MTrk" + struct.pack(">I", len(trk)) + trk)

    print(f"channel {ch + 1}: {len(mono)} notes, transpose {t:+d}, "
          f"{moved} moved by an octave, wrote {args.dst}")


if __name__ == "__main__":
    main()
