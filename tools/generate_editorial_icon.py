"""Export the existing two-paper vector mark as PNG and Windows ICO (stdlib only)."""
from pathlib import Path
import struct
import zlib

root = Path(__file__).resolve().parents[1] / "assets" / "editorial"
shapes = [([(38, 22), (57, 11), (77, 17), (57, 30)], (214, 64, 44)),
          ([(38, 32), (57, 23), (77, 30), (57, 43)], (18, 43, 71))]


def inside(x, y, points):
    return all((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) >= 0
               for a, b in zip(points, points[1:] + points[:1]))


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def png(size):
    rows = bytearray()
    for py in range(size):
        rows.append(0)
        for px in range(size):
            hits = []
            for dy in (.125, .375, .625, .875):
                for dx in (.125, .375, .625, .875):
                    x = (px + dx) / size * 48 + 33.5
                    y = (py + dy) / size * 48 + 3
                    color = None
                    for points, ink in shapes:
                        if inside(x, y, points):
                            color = ink
                    if color:
                        hits.append(color)
            rows.extend(tuple(round(sum(c[i] for c in hits) / len(hits)) for i in range(3))
                        + (round(len(hits) / 16 * 255),) if hits else (0, 0, 0, 0))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


sizes = [16, 24, 32, 48, 64, 128, 256]
images = [png(size) for size in sizes]
offset = 6 + 16 * len(sizes)
entries = bytearray()
for size, data in zip(sizes, images):
    (root / f"brand-{size}.png").write_bytes(data)
    entries.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
    offset += len(data)
(root / "paperroom.ico").write_bytes(struct.pack("<HHH", 0, 1, len(sizes)) + entries + b"".join(images))
print("Exported paperroom.ico and seven PNG sizes from the existing vector mark.")
