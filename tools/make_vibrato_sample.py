"""Write a vibrato test: the chromatic scale B3 to F5, every note with vibrato.

usage: python make_vibrato_sample.py [out.mid] [depth 0-127]
"""
import struct
import sys

LOW, HIGH = 59, 77
NOTE_MS, GAP_MS = 800, 200
DIVISION = 500  # ticks per quarter at 500000 us/quarter: 1 tick = 1 ms


def varlen(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "vibrato_sample.mid"
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 96
    trk = varlen(0) + b"\xff\x51\x03" + (500000).to_bytes(3, "big")
    trk += varlen(0) + bytes([0xB0, 1, depth])
    for i, pitch in enumerate(range(LOW, HIGH + 1)):
        trk += varlen(0 if i == 0 else GAP_MS) + bytes([0x90, pitch, 100])
        trk += varlen(NOTE_MS) + bytes([0x80, pitch, 0])
    trk += b"\x00\xff\x2f\x00"
    with open(dst, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, DIVISION))
        f.write(b"MTrk" + struct.pack(">I", len(trk)) + trk)


if __name__ == "__main__":
    main()
