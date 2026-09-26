# 一次性生成图层面板底栏线框图标（浅灰透明底 PNG）
import math
import os
import struct
import zlib

OUT = os.path.dirname(os.path.abspath(__file__))
SIZE = 48
FG = (220, 220, 220, 255)


def new_buf():
    return [[(0, 0, 0, 0) for _ in range(SIZE)] for _ in range(SIZE)]


def setp(buf, x, y, c=FG):
    if 0 <= x < SIZE and 0 <= y < SIZE:
        buf[y][x] = c


def disc(buf, cx, cy, r, c=FG):
    r2 = r * r
    for y in range(int(cy - r) - 1, int(cy + r) + 2):
        for x in range(int(cx - r) - 1, int(cx + r) + 2):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                setp(buf, x, y, c)


def ring(buf, cx, cy, r, t=2.2, c=FG):
    r2o = (r + t / 2) ** 2
    r2i = max(0.0, (r - t / 2) ** 2)
    for y in range(int(cy - r - t) - 1, int(cy + r + t) + 2):
        for x in range(int(cx - r - t) - 1, int(cx + r + t) + 2):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if r2i <= d2 <= r2o:
                setp(buf, x, y, c)


def line(buf, x0, y0, x1, y1, t=2.4, c=FG):
    dx, dy = x1 - x0, y1 - y0
    dist = math.hypot(dx, dy) or 1.0
    n = int(dist * 2) + 1
    for i in range(n + 1):
        x = x0 + dx * i / n
        y = y0 + dy * i / n
        disc(buf, x, y, t / 2, c)


def rect_stroke(buf, x0, y0, x1, y1, t=2.2, c=FG):
    line(buf, x0, y0, x1, y0, t, c)
    line(buf, x1, y0, x1, y1, t, c)
    line(buf, x1, y1, x0, y1, t, c)
    line(buf, x0, y1, x0, y0, t, c)


def fill_rect(buf, x0, y0, x1, y1, c=FG):
    for y in range(int(y0), int(y1) + 1):
        for x in range(int(x0), int(x1) + 1):
            setp(buf, x, y, c)


def save_png(buf, path):
    raw = b""
    for row in buf:
        raw += b"\x00"
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))
    compressed = zlib.compress(raw, 9)

    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    data = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", compressed)
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", path)


def gen_link():
    b = new_buf()
    ring(b, 18, 24, 9, 2.6)
    ring(b, 30, 24, 9, 2.6)
    save_png(b, os.path.join(OUT, "link.png"))


def gen_fx():
    b = new_buf()
    fill_rect(b, 8, 12, 11, 36)
    fill_rect(b, 8, 12, 22, 15)
    fill_rect(b, 8, 22, 18, 25)
    line(b, 26, 14, 40, 36, 2.8)
    line(b, 40, 14, 26, 36, 2.8)
    save_png(b, os.path.join(OUT, "fx.png"))


def gen_mask():
    b = new_buf()
    rect_stroke(b, 10, 10, 38, 38, 2.4)
    for y in range(SIZE):
        for x in range(SIZE):
            if (x - 24) ** 2 + (y - 24) ** 2 <= 9**2:
                setp(b, x, y, FG)
    save_png(b, os.path.join(OUT, "mask.png"))


def gen_adjustment():
    b = new_buf()
    ring(b, 24, 24, 14, 2.6)
    for y in range(SIZE):
        for x in range(SIZE):
            if x >= 24 and (x - 24) ** 2 + (y - 24) ** 2 <= 13**2:
                setp(b, x, y, FG)
    save_png(b, os.path.join(OUT, "adjustment.png"))


def gen_group():
    b = new_buf()
    fill_rect(b, 8, 14, 22, 18)
    rect_stroke(b, 8, 18, 40, 36, 2.4)
    line(b, 8, 22, 40, 22, 2.0)
    save_png(b, os.path.join(OUT, "group.png"))


def gen_new_layer():
    b = new_buf()
    rect_stroke(b, 10, 14, 30, 34, 2.2)
    rect_stroke(b, 16, 10, 36, 30, 2.2)
    line(b, 34, 36, 42, 36, 2.6)
    line(b, 38, 32, 38, 40, 2.6)
    save_png(b, os.path.join(OUT, "new-layer.png"))


def gen_delete():
    b = new_buf()
    line(b, 14, 14, 34, 14, 2.4)
    line(b, 18, 10, 30, 10, 2.2)
    line(b, 18, 10, 18, 14, 2.0)
    line(b, 30, 10, 30, 14, 2.0)
    rect_stroke(b, 14, 16, 34, 38, 2.4)
    line(b, 20, 20, 20, 34, 2.0)
    line(b, 24, 20, 24, 34, 2.0)
    line(b, 28, 20, 28, 34, 2.0)
    save_png(b, os.path.join(OUT, "delete.png"))


if __name__ == "__main__":
    gen_link()
    gen_fx()
    gen_mask()
    gen_adjustment()
    gen_group()
    gen_new_layer()
    gen_delete()
    print("done")
