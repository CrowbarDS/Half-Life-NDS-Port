#!/usr/bin/env python3
"""The map a card's new game (and a debug start) begins in: read, or set.

usage: setstart.py <manifest.bin> [<map>]

The manifest is shared/HlManifest.hpp's: a 16-byte header (magic, version,
level format, payload bytes, the payload's CRC-32), then the payload, yas
binary: the converter's version and the start map, each a 64-bit length and
its bytes, then the rest, left as it is. Back the file up before setting it.
"""
import struct
import sys
import zlib


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    path = sys.argv[1]
    data = open(path, "rb").read()
    magic, version, level, size, crc = struct.unpack_from("<4sHHII", data, 0)
    payload = data[16:]
    if magic != b"HLMF":
        sys.exit("not a manifest: " + path)
    if len(payload) != size or zlib.crc32(payload) != crc:
        sys.exit("manifest damaged: " + path)
    first = struct.unpack_from("<Q", payload, 0)[0]
    at = 8 + first
    length = struct.unpack_from("<Q", payload, at)[0]
    start = payload[at + 8:at + 8 + length].decode()
    if len(sys.argv) == 2:
        print(start)
        return
    new = sys.argv[2].encode()
    payload = payload[:at] + struct.pack("<Q", len(new)) + new + payload[at + 8 + length:]
    header = struct.pack("<4sHHII", magic, version, level, len(payload), zlib.crc32(payload))
    open(path, "wb").write(header + payload)
    print(f"start map {start} -> {sys.argv[2]}")


if __name__ == "__main__":
    main()
