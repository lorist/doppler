#!/usr/bin/env python3
"""Generate the "Dark Frosted" idle backdrop for pexninja.

Pulse composites this image behind the video panes; without one it draws its
own bright-red "Missing background image" placeholder, which is the first thing
you see before a call connects.

This reproduces the backdrop of variant 01 "Dark Frosted" from the Pexip
redesign canvas ("Pexip App - Design Alternatives"), so the Pulse-rendered
background and the ImGui chrome in PexipTheme.h are the same design:

    background: linear-gradient(135deg, #0d1220 0%, #111827 40%, #0d1a2a 100%)

    mesh blob 1: radial-gradient(circle, rgba(99,102,241,0.18), transparent 70%)
                 320x320 at top:-80 left:-60      (indigo)
    mesh blob 2: radial-gradient(circle, rgba(56,189,248,0.12), transparent 70%)
                 280x280 at bottom:-60 right:-40  (sky)

The design's artboard is 820x580; the blob geometry below is scaled from that
reference frame to the 1920x1080 output so the composition matches.

Pure stdlib (zlib + struct) - no Pillow or ImageMagick needed.

    python3 make-background.py [output.png]
"""

import math
import struct
import sys
import zlib

WIDTH, HEIGHT = 1920, 1080

# Design reference artboard, used to scale the blob geometry.
REF_W, REF_H = 820, 580
SX, SY = WIDTH / REF_W, HEIGHT / REF_H

# linear-gradient(135deg, ...) stops.
STOPS = [
    (0.00, (0x0D, 0x12, 0x20)),
    (0.40, (0x11, 0x18, 0x27)),
    (1.00, (0x0D, 0x1A, 0x2A)),
]

# (centre_x, centre_y, radius, colour, peak_alpha) in output pixels.
# CSS insets are relative to the artboard, then scaled.
BLOBS = [
    # top:-80 left:-60, 320x320  ->  centre (-60+160, -80+160), r=160
    ((-60 + 160) * SX, (-80 + 160) * SY, 160 * ((SX + SY) / 2), (99, 102, 241), 0.18),
    # bottom:-60 right:-40, 280x280  ->  centre (820-(-40)-140, 580-(-60)-140)
    ((REF_W + 40 - 140) * SX, (REF_H + 60 - 140) * SY, 140 * ((SX + SY) / 2), (56, 189, 248), 0.12),
]


def gradient_at(t):
    """Sample the multi-stop linear gradient at position t in [0, 1]."""
    t = min(max(t, 0.0), 1.0)
    for i in range(len(STOPS) - 1):
        t0, c0 = STOPS[i]
        t1, c1 = STOPS[i + 1]
        if t <= t1:
            f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(c0[j] + (c1[j] - c0[j]) * f for j in range(3))
    return STOPS[-1][1]


# 4x4 ordered (Bayer) dither matrix, normalised to [-0.5, +0.5).
# Very smooth gradients over a wide area quantise into visible concentric
# banding at 8 bits; nudging each channel by under half a level before
# rounding breaks the bands up without adding perceptible noise.
_BAYER4 = [
    [0, 8, 2, 10],
    [12, 4, 14, 6],
    [3, 11, 1, 9],
    [15, 7, 13, 5],
]
DITHER = [[(_BAYER4[y][x] + 0.5) / 16.0 - 0.5 for x in range(4)] for y in range(4)]


def build_rows():
    """Yield the full raw scanline block, each row prefixed with its filter byte."""
    # CSS 135deg runs top-left -> bottom-right. Project each pixel onto that
    # axis and normalise by the axis length.
    axis_len = WIDTH + HEIGHT

    # Precompute the gradient per projection step so the inner loop stays cheap.
    ramp = [gradient_at(i / axis_len) for i in range(axis_len + 1)]

    rows = []
    for y in range(HEIGHT):
        row = bytearray()
        row.append(0)  # PNG filter type 0 (None)
        dither_row = DITHER[y & 3]
        for x in range(WIDTH):
            dither = dither_row[x & 3]
            r, g, b = ramp[x + y]

            # Additive mesh blobs, matching CSS radial-gradient(..., transparent 70%):
            # full strength at the centre, fading to nothing at 1/0.7 of the radius.
            for cx, cy, rad, (br, bg_, bb), peak in BLOBS:
                dx, dy = x - cx, y - cy
                d = math.sqrt(dx * dx + dy * dy)
                reach = rad / 0.7
                if d < reach:
                    a = (1.0 - d / reach) * peak
                    r += (br - r) * a
                    g += (bg_ - g) * a
                    b += (bb - b) * a

            row += bytes(
                (
                    min(255, max(0, int(r + dither + 0.5))),
                    min(255, max(0, int(g + dither + 0.5))),
                    min(255, max(0, int(b + dither + 0.5))),
                )
            )
        rows.append(bytes(row))
    return b"".join(rows)


def chunk(tag, data):
    out = struct.pack(">I", len(data)) + tag + data
    return out + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "pexninja-background.png"

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)  # 8-bit truecolour
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(build_rows(), 9))
        + chunk(b"IEND", b"")
    )

    with open(out_path, "wb") as fh:
        fh.write(png)

    print(f"wrote {out_path} ({WIDTH}x{HEIGHT}, {len(png):,} bytes)")


if __name__ == "__main__":
    main()
