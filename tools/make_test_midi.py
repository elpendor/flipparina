"""Write chromatic.mid and repeats.mid test files."""
import struct
import sys
from pathlib import Path

TPQ = 480          # ticks per quarter note
TEMPO = 500_000    # us per quarter = 120 BPM


def varlen(v: int) -> bytes:
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def ms_to_ticks(ms: float) -> int:
    return round(ms * 1000 * TPQ / TEMPO)


def write_midi(path: Path, notes):
    events = []
    for pitch, on, off in notes:
        events.append((ms_to_ticks(on), 1, bytes([0x90, pitch, 100])))
        events.append((ms_to_ticks(off), 0, bytes([0x80, pitch, 0])))
    events.sort(key=lambda e: (e[0], e[1]))

    track = varlen(0) + b"\xff\x51\x03" + TEMPO.to_bytes(3, "big")
    last = 0
    for tick, _, data in events:
        track += varlen(tick - last) + data
        last = tick
    track += varlen(0) + b"\xff\x2f\x00"

    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ)
    path.write_bytes(header + b"MTrk" + struct.pack(">I", len(track)) + track)
    print(f"wrote {path}")


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    out.mkdir(parents=True, exist_ok=True)

    chromatic = [(p, i * 700, i * 700 + 600) for i, p in enumerate(range(60, 77))]
    write_midi(out / "chromatic.mid", chromatic)

    repeats, t = [], 0
    for length in (400, 300, 200, 150, 100, 75):
        for _ in range(4):
            repeats.append((62, t, t + length))
            t += length
        t += 600
    write_midi(out / "repeats.mid", repeats)


if __name__ == "__main__":
    main()
