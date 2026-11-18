"""Add vibrato (modulation, CC1) to the long notes of a MIDI file.

usage: python add_vibrato.py in.mid out.mid [--depth 0-127] [--min-beats 0.75] [--channel N]
"""
import argparse
import struct


def varlen_read(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def read_track(d):
    """Events as (tick, bytes), with running status expanded."""
    events, i, tick, running = [], 0, 0, 0
    while i < len(d):
        delta, i = varlen_read(d, i)
        tick += delta
        b = d[i]
        if b == 0xFF:
            length, j = varlen_read(d, i + 2)
            events.append((tick, d[i:j + length]))
            i = j + length
        elif b in (0xF0, 0xF7):
            length, j = varlen_read(d, i + 1)
            events.append((tick, d[i:j + length]))
            i = j + length
        else:
            if b & 0x80:
                running = b
                i += 1
            size = 1 if running & 0xF0 in (0xC0, 0xD0) else 2
            events.append((tick, bytes([running]) + d[i:i + size]))
            i += size
    return events


def is_off(e):
    return (e[0] & 0xF0 == 0x80) or (e[0] & 0xF0 == 0x90 and len(e) == 3 and e[2] == 0)


def add_vibrato(events, depth, min_ticks, channel):
    added, held = [], {}
    for tick, e in events:
        if e[0] & 0xF0 not in (0x80, 0x90) or e[0] & 0x0F == 9:
            continue
        ch = e[0] & 0x0F
        if channel is not None and ch != channel:
            continue
        if is_off(e):
            start = held.pop((ch, e[1]), None)
            if start is not None and tick - start >= min_ticks:
                added.append((start, 2, bytes([0xB0 | ch, 1, depth])))
                added.append((tick, 1, bytes([0xB0 | ch, 1, 0])))
        else:
            held[(ch, e[1])] = tick
    # At equal ticks: note-offs, then vibrato off, then vibrato on, then everything else
    keyed = [(t, 0 if is_off(e) else 3, n, e) for n, (t, e) in enumerate(events)]
    keyed += [(t, p, 0, e) for t, p, e in added]
    keyed.sort(key=lambda k: k[:3])
    return [(t, e) for t, _, _, e in keyed], len(added) // 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--depth", type=int, default=80)
    ap.add_argument("--min-beats", type=float, default=0.75)
    ap.add_argument("--channel", type=int)
    args = ap.parse_args()

    d = open(args.src, "rb").read()
    if d[:4] != b"MThd":
        raise SystemExit("not a MIDI file")
    header_len = struct.unpack(">I", d[4:8])[0]
    division = struct.unpack(">H", d[12:14])[0]
    if division & 0x8000:
        raise SystemExit("SMPTE timing is not supported")
    out, pos, total = d[:8 + header_len], 8 + header_len, 0
    while pos + 8 <= len(d):
        cid, length = d[pos:pos + 4], struct.unpack(">I", d[pos + 4:pos + 8])[0]
        body = d[pos + 8:pos + 8 + length]
        pos += 8 + length
        if cid != b"MTrk":
            out += cid + struct.pack(">I", length) + body
            continue
        events, count = add_vibrato(
            read_track(body), args.depth, int(division * args.min_beats), args.channel)
        total += count
        trk, last = b"", 0
        for tick, e in events:
            trk += varlen(tick - last) + e
            last = tick
        out += b"MTrk" + struct.pack(">I", len(trk)) + trk
    open(args.dst, "wb").write(out)
    print(f"vibrato on {total} notes -> {args.dst}")


if __name__ == "__main__":
    main()
