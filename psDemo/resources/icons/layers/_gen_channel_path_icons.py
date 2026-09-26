# 生成通道 / 路径面板底栏线框图标
# 输出：resources/icons/channels/ 、resources/icons/paths/
import math
import os
import struct
import zlib

ICONS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
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


def line(buf, x0, y0, x1, y1, t=2.4, c=FG):
    dx, dy = x1 - x0, y1 - y0
    dist = math.hypot(dx, dy) or 1.0
    n = int(dist * 2) + 1
    for i in range(n + 1):
        disc(buf, x0 + dx * i / n, y0 + dy * i / n, t / 2, c)


def dashed_ring(buf, cx, cy, r, t=2.2, dash=5, gap=3, c=FG):
    circ = 2 * math.pi * r
    step = 0.5
    pos = 0.0
    on = True
    remain = float(dash)
    while pos < circ:
        ang = pos / r
        x = cx + r * math.cos(ang)
        y = cy + r * math.sin(ang)
        if on:
            disc(buf, x, y, t / 2, c)
        pos += step
        remain -= step
        if remain <= 0:
            on = not on
            remain = float(dash if on else gap)


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
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    print("wrote", path)


def point_in_poly(x, y, pts):
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if ((yi > y) != (yj > y)) and (
            x < (xj - xi) * (y - yi) / ((yj - yi) or 1e-6) + xi
        ):
            inside = not inside
        j = i
    return inside


def gen_channels():
    out = os.path.join(ICONS, "channels")
    b = new_buf()
    dashed_ring(b, 22, 24, 12, 2.4, dash=4, gap=3)
    line(b, 34, 24, 42, 24, 2.4)
    line(b, 38, 20, 42, 24, 2.2)
    line(b, 38, 28, 42, 24, 2.2)
    save_png(b, os.path.join(out, "load-selection.png"))

    b = new_buf()
    dashed_ring(b, 18, 24, 11, 2.4, dash=4, gap=3)
    rect_stroke(b, 30, 16, 42, 32, 2.2)
    fill_rect(b, 33, 19, 39, 29)
    save_png(b, os.path.join(out, "save-selection.png"))

    b = new_buf()
    rect_stroke(b, 10, 12, 30, 36, 2.2)
    line(b, 14, 18, 26, 18, 2.0)
    line(b, 14, 24, 26, 24, 2.0)
    line(b, 14, 30, 26, 30, 2.0)
    line(b, 34, 36, 42, 36, 2.6)
    line(b, 38, 32, 38, 40, 2.6)
    save_png(b, os.path.join(out, "new-channel.png"))


def gen_paths():
    out = os.path.join(ICONS, "paths")
    pts = [(24, 10), (38, 20), (32, 38), (16, 38), (10, 20)]

    b = new_buf()
    for y in range(SIZE):
        for x in range(SIZE):
            if point_in_poly(x + 0.5, y + 0.5, pts):
                setp(b, x, y, FG)
    for i in range(len(pts)):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % len(pts)]
        line(b, x0, y0, x1, y1, 2.0)
    save_png(b, os.path.join(out, "fill.png"))

    b = new_buf()
    for i in range(len(pts)):
        x0, y0 = pts[i]
        x1, y1 = pts[(i + 1) % len(pts)]
        line(b, x0, y0, x1, y1, 2.6)
        disc(b, x0, y0, 2.2)
    save_png(b, os.path.join(out, "stroke.png"))

    b = new_buf()
    line(b, 10, 30, 18, 14, 2.4)
    line(b, 18, 14, 28, 28, 2.4)
    line(b, 28, 28, 36, 12, 2.4)
    disc(b, 10, 30, 2.4)
    disc(b, 18, 14, 2.4)
    disc(b, 28, 28, 2.4)
    disc(b, 36, 12, 2.4)
    dashed_ring(b, 36, 34, 8, 2.0, dash=3, gap=2)
    save_png(b, os.path.join(out, "to-selection.png"))

    b = new_buf()
    dashed_ring(b, 16, 24, 10, 2.2, dash=3, gap=2)
    line(b, 28, 32, 34, 16, 2.4)
    line(b, 34, 16, 42, 30, 2.4)
    disc(b, 28, 32, 2.4)
    disc(b, 34, 16, 2.4)
    disc(b, 42, 30, 2.4)
    save_png(b, os.path.join(out, "from-selection.png"))

    b = new_buf()
    line(b, 16, 34, 24, 12, 2.6)
    line(b, 24, 12, 28, 18, 2.4)
    line(b, 28, 18, 20, 36, 2.4)
    line(b, 16, 34, 20, 36, 2.2)
    disc(b, 24, 12, 2.0)
    line(b, 34, 36, 42, 36, 2.6)
    line(b, 38, 32, 38, 40, 2.6)
    save_png(b, os.path.join(out, "new-path.png"))


if __name__ == "__main__":
    gen_channels()
    gen_paths()
    print("done")
