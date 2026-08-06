#!/usr/bin/env python3
"""Build an iOS AppIcon.appiconset from Orca's macOS icon.

Orca ships resources/images/OrcaSlicer.icns, whose largest member (ic10) is a
1024x1024 PNG. It cannot be used as an iOS icon as it is:

  - it is a macOS icon, so the artwork is a rounded square inset inside a
    transparent margin - only 56.5% of the canvas is opaque. iOS applies its own
    rounded-rect mask to the full square, so the result would be a small icon
    floating in the middle of a black field.
  - iOS icons must be opaque. Alpha is composited against black, which would put
    black into the rounded corners of the artwork itself.

So: crop to the opaque bounding box, composite onto the artwork's own corner
colour, and box-downscale to each size iOS asks for.

Pure stdlib on purpose - the runner has no Pillow, and installing one for three
hundred kilobytes of PNG would cost more than the build it is part of. Only the
PNG features Orca's icon actually uses are handled (8-bit RGBA, no interlace),
and anything else fails loudly rather than silently producing a broken icon.
"""

import json
import os
import struct
import sys
import zlib

# (size in px, idiom, point size, scale) for every slot iOS wants. UIDeviceFamily
# is iPad-only today, but a complete set costs nothing and stops actool warning.
SLOTS = [
    (40,   "iphone",       "20x20",     "2x"),
    (60,   "iphone",       "20x20",     "3x"),
    (58,   "iphone",       "29x29",     "2x"),
    (87,   "iphone",       "29x29",     "3x"),
    (80,   "iphone",       "40x40",     "2x"),
    (120,  "iphone",       "40x40",     "3x"),
    (120,  "iphone",       "60x60",     "2x"),
    (180,  "iphone",       "60x60",     "3x"),
    (20,   "ipad",         "20x20",     "1x"),
    (40,   "ipad",         "20x20",     "2x"),
    (29,   "ipad",         "29x29",     "1x"),
    (58,   "ipad",         "29x29",     "2x"),
    (40,   "ipad",         "40x40",     "1x"),
    (80,   "ipad",         "40x40",     "2x"),
    (76,   "ipad",         "76x76",     "1x"),
    (152,  "ipad",         "76x76",     "2x"),
    (167,  "ipad",         "83.5x83.5", "2x"),
    (1024, "ios-marketing", "1024x1024", "1x"),
]


def icns_largest_png(data):
    """Return the ic10 (1024x1024) member of an .icns, which is a whole PNG."""
    if data[:4] != b"icns":
        raise SystemExit("not an icns file")
    best = None
    pos = 8
    while pos < len(data) - 8:
        kind = data[pos:pos + 4]
        size = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        if size < 8:
            break
        body = data[pos + 8:pos + size]
        if body[:8] == b"\x89PNG\r\n\x1a\x0a":
            w = struct.unpack(">I", body[16:20])[0]
            if best is None or w > best[0]:
                best = (w, body)
        pos += size
    if best is None:
        raise SystemExit("icns contains no PNG member")
    return best[1]


def png_decode_rgba(png):
    """Decode an 8-bit RGBA non-interlaced PNG to (w, h, bytearray)."""
    if png[:8] != b"\x89PNG\r\n\x1a\x0a":
        raise SystemExit("not a PNG")
    w, h = struct.unpack(">II", png[16:24])
    depth, colour, _comp, _filt, interlace = png[24], png[25], png[26], png[27], png[28]
    if (depth, colour, interlace) != (8, 6, 0):
        raise SystemExit(
            "unsupported PNG: depth=%d colour=%d interlace=%d (want 8/6/0)"
            % (depth, colour, interlace))

    idat = b""
    pos = 8
    while pos < len(png):
        length = struct.unpack(">I", png[pos:pos + 4])[0]
        if png[pos + 4:pos + 8] == b"IDAT":
            idat += png[pos + 8:pos + 8 + length]
        pos += 12 + length

    raw = zlib.decompress(idat)
    stride = w * 4
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ftype = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ftype == 1:
            for x in range(4, stride):
                line[x] = (line[x] + line[x - 4]) & 255
        elif ftype == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif ftype == 3:
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif ftype == 4:
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        elif ftype != 0:
            raise SystemExit("bad PNG filter %d" % ftype)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, out


def png_encode_rgb(w, h, rgb):
    """Encode 8-bit RGB (no alpha - iOS icons must be opaque)."""
    raw = bytearray()
    stride = w * 3
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\x0a"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def opaque_bbox(w, h, px, threshold=8):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if px[(row + x) * 4 + 3] > threshold:
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    if x1 < 0:
        raise SystemExit("icon is fully transparent")
    return x0, y0, x1 + 1, y1 + 1


def flatten_crop(w, h, px, box, bg):
    """Crop to box and composite onto bg, returning a square RGB image."""
    x0, y0, x1, y1 = box
    cw, ch = x1 - x0, y1 - y0
    side = max(cw, ch)                     # keep it square; centre the artwork
    ox, oy = (side - cw) // 2, (side - ch) // 2
    out = bytearray(side * side * 3)
    for i in range(0, len(out), 3):
        out[i], out[i + 1], out[i + 2] = bg
    for y in range(ch):
        src = ((y0 + y) * w + x0) * 4
        dst = ((oy + y) * side + ox) * 3
        for x in range(cw):
            r, g, b, a = px[src:src + 4]
            if a == 255:
                out[dst] , out[dst+1], out[dst+2] = r, g, b
            elif a:
                out[dst]   = (r * a + bg[0] * (255 - a)) // 255
                out[dst+1] = (g * a + bg[1] * (255 - a)) // 255
                out[dst+2] = (b * a + bg[2] * (255 - a)) // 255
            src += 4
            dst += 3
    return side, out


def box_resize(side, rgb, target):
    """Box-filter downscale of a square RGB image."""
    out = bytearray(target * target * 3)
    for ty in range(target):
        sy0, sy1 = ty * side // target, max((ty + 1) * side // target, ty * side // target + 1)
        for tx in range(target):
            sx0, sx1 = tx * side // target, max((tx + 1) * side // target, tx * side // target + 1)
            r = g = b = n = 0
            for sy in range(sy0, sy1):
                base = sy * side
                for sx in range(sx0, sx1):
                    i = (base + sx) * 3
                    r += rgb[i]; g += rgb[i + 1]; b += rgb[i + 2]
                    n += 1
            i = (ty * target + tx) * 3
            out[i], out[i + 1], out[i + 2] = r // n, g // n, b // n
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: make-ios-icons.py <OrcaSlicer.icns> <out.xcassets>")
    src, outdir = sys.argv[1], sys.argv[2]

    data = open(src, "rb").read()
    png = icns_largest_png(data) if data[:4] == b"icns" else data
    w, h, px = png_decode_rgba(png)

    box = opaque_bbox(w, h, px)
    print("source %dx%d, artwork bbox %s (%.1f%% of canvas)"
          % (w, h, box, 100.0 * (box[2] - box[0]) * (box[3] - box[1]) / (w * h)))

    # Background: the artwork's own top-left opaque pixel, so a light icon stays
    # light and a dark one stays dark instead of being guessed at.
    bx, by = box[0] + (box[2] - box[0]) // 2, box[1] + 2
    bg = tuple(px[(by * w + bx) * 4 + k] for k in range(3))
    print("flattening onto rgb%s" % (bg,))

    side, flat = flatten_crop(w, h, px, box, bg)

    iconset = os.path.join(outdir, "AppIcon.appiconset")
    os.makedirs(iconset, exist_ok=True)

    images, cache = [], {}
    for size, idiom, pt, scale in SLOTS:
        name = "AppIcon-%d.png" % size
        if size not in cache:
            scaled = flat if size == side else box_resize(side, flat, size)
            open(os.path.join(iconset, name), "wb").write(png_encode_rgb(size, size, scaled))
            cache[size] = name
        images.append({"size": pt, "idiom": idiom, "filename": cache[size], "scale": scale})

    with open(os.path.join(iconset, "Contents.json"), "w") as fh:
        json.dump({"images": images, "info": {"version": 1, "author": "xcode"}}, fh, indent=2)
    with open(os.path.join(outdir, "Contents.json"), "w") as fh:
        json.dump({"info": {"version": 1, "author": "xcode"}}, fh, indent=2)

    print("wrote %d PNGs to %s" % (len(cache), iconset))


if __name__ == "__main__":
    main()
