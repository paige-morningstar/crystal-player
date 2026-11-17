#!/usr/bin/env python3
"""Renders the app art: icon.png, banner.png and banner.wav.

    python3 tools/make_assets.py [output-dir]     (default: resources)

The banner is then packaged with bannertool:

    bannertool makebanner -i resources/banner.png -a resources/banner.wav \
        -o resources/banner.bnr

Pure stdlib, so it needs no image or audio libraries.
"""

import math
import os
import struct
import sys
import wave
import zlib

ACCENT = (79, 195, 247)
ACCENT_DIM = (58, 138, 178)
FOREGROUND = (236, 241, 248)

# 5x7 uppercase glyphs, enough for "CRYSTAL PLAYER".
FONT = {
    " ": ["00000"] * 7,
    "A": ["00100", "01010", "10001", "10001", "11111", "10001", "10001"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
}


class Canvas:
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.px = [[(0, 0, 0, 0)] * w for _ in range(h)]

    def put(self, x, y, color, alpha=255):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = (color[0], color[1], color[2], alpha)

    def gradient(self, top, bottom):
        for y in range(self.h):
            t = y / max(1, self.h - 1)
            row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
            for x in range(self.w):
                self.put(x, y, row)

    def rect(self, x0, y0, x1, y1, color):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.put(x, y, color)

    def ellipse(self, cx, cy, rx, ry, color):
        for y in range(int(cy - ry), int(cy + ry) + 1):
            for x in range(int(cx - rx), int(cx + rx) + 1):
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.put(x, y, color)

    def round_corners(self, radius):
        r = float(radius)
        for y in range(self.h):
            for x in range(self.w):
                cx = min(max(x, r), self.w - 1 - r)
                cy = min(max(y, r), self.h - 1 - r)
                if (x - cx) ** 2 + (y - cy) ** 2 > r * r:
                    self.px[y][x] = (0, 0, 0, 0)

    def text(self, x, y, s, scale, color, spacing=1):
        cx = x
        for ch in s:
            glyph = FONT.get(ch.upper())
            if glyph:
                for gy, row in enumerate(glyph):
                    for gx, bit in enumerate(row):
                        if bit == "1":
                            self.rect(cx + gx * scale, y + gy * scale,
                                      cx + (gx + 1) * scale, y + (gy + 1) * scale, color)
            cx += (5 + spacing) * scale
        return cx

    def write_png(self, path):
        raw = bytearray()
        for row in self.px:
            raw.append(0)
            for px in row:
                raw.extend(px)

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        png += chunk(b"IEND", b"")
        with open(path, "wb") as f:
            f.write(png)
        print("wrote %s (%dx%d, %d bytes)" % (path, self.w, self.h, len(png)))


def draw_note(c, cx, cy, size, color):
    """Filled music note: note head, stem and flag."""
    c.ellipse(cx - size * 0.18, cy + size * 0.22, size * 0.26, size * 0.20, color)
    c.rect(int(cx + size * 0.04), int(cy - size * 0.48),
           int(cx + size * 0.13), int(cy + size * 0.24), color)
    stem_x = int(cx + size * 0.13)
    for i in range(int(size * 0.26)):
        c.rect(stem_x, int(cy - size * 0.48) + i,
               stem_x + int(size * 0.30) - i // 3, int(cy - size * 0.48) + i + 1, color)


def make_icon(path):
    c = Canvas(48, 48)
    c.gradient((14, 18, 26), (24, 32, 46))
    draw_note(c, 25.0, 25.0, 30.0, ACCENT)
    c.round_corners(9)
    c.write_png(path)


def make_banner(path):
    c = Canvas(256, 128)
    c.gradient((10, 14, 22), (26, 36, 54))

    # soft accent glow behind the note
    for y in range(128):
        for x in range(96):
            d = math.hypot(x - 44, y - 64)
            if d < 46:
                glow = 1.0 - d / 46.0
                r, g, b = c.px[y][x][:3]
                c.put(x, y, (min(255, int(r + 9 * glow)),
                             min(255, int(g + 26 * glow)),
                             min(255, int(b + 40 * glow))))

    draw_note(c, 46.0, 60.0, 56.0, ACCENT)

    c.text(100, 30, "CRYSTAL", 3, FOREGROUND)
    c.text(100, 58, "PLAYER", 3, ACCENT)

    # equaliser hairline, tying the banner to the player theme
    for i, height in enumerate([4, 9, 14, 7, 11, 5, 8, 12, 6, 10]):
        x = 100 + i * 14
        for k in range(height):
            c.rect(x, 116 - k, x + 6, 117 - k, ACCENT_DIM)

    c.write_png(path)


def make_banner_audio(path):
    """Short two-note chime; bannertool requires a tune for the banner."""
    rate = 32728
    length = 1.6
    total = int(rate * length)
    frames = []
    for i in range(total):
        t = i / rate
        value = 0.0
        for freq, delay in ((659.25, 0.0), (880.0, 0.28)):
            dt = t - delay
            if dt < 0:
                continue
            envelope = min(1.0, dt / 0.008) * math.exp(-2.6 * dt)
            value += 0.45 * envelope * (math.sin(2 * math.pi * freq * dt)
                                        + 0.22 * math.sin(4 * math.pi * freq * dt))
        value *= min(1.0, (total - i) / (rate * 0.15))
        frames.append(int(max(-1.0, min(1.0, value * 0.55)) * 32767))

    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % total, *frames))
    print("wrote %s (%.1fs, %d Hz mono)" % (path, length, rate))


def main(outdir):
    make_icon(os.path.join(outdir, "icon.png"))
    make_banner(os.path.join(outdir, "banner.png"))
    make_banner_audio(os.path.join(outdir, "banner.wav"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "resources")
