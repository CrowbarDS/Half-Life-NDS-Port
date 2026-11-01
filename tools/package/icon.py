#!/usr/bin/env python3
"""icon.py <Logo.bmp> <out.png>: the ROM's icon, four times the size, for the
AppImage. Standard library only: a 4-bit BMP in, a PNG out."""
import struct
import sys
import zlib


def main(src, dst):
    data = open(src, 'rb').read()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    palette = [data[54 + i * 4:54 + i * 4 + 3][::-1] for i in range(16)]
    stride = (width // 2 + 3) & ~3
    scale = 4
    rows = []
    for y in range(height - 1, -1, -1):
        row = data[offset + y * stride:offset + (y + 1) * stride]
        pixels = b''.join(palette[(row[x // 2] >> (0 if x & 1 else 4)) & 15] * scale for x in range(width))
        rows.extend([b'\0' + pixels] * scale)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width * scale, height * scale, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
