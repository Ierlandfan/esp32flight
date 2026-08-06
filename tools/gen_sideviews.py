#!/usr/bin/env python3
"""Generate main/img_sideviews.c: side-view aircraft silhouettes for the
ambient selection overlay (64x28, LV_IMG_CF_TRUE_COLOR_ALPHA, white +
alpha so lv_img recolor works like the map sprites). Pure stdlib; shapes
are rasterized 4x supersampled for soft edges. --preview writes a PNG
montage instead of the C file."""
import struct
import sys
import zlib

W, H, SS = 64, 28, 4          # target size, supersample factor
SW, SH = W * SS, H * SS


def canvas():
    return [[0.0] * SW for _ in range(SH)]


def poly(c, pts, val=1.0):
    """Even-odd scanline fill; pts in 64x28 space."""
    p = [(x * SS, y * SS) for x, y in pts]
    for sy in range(SH):
        yc = sy + 0.5
        xs = []
        for i in range(len(p)):
            x1, y1 = p[i]
            x2, y2 = p[(i + 1) % len(p)]
            if (y1 <= yc < y2) or (y2 <= yc < y1):
                xs.append(x1 + (yc - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for j in range(0, len(xs) - 1, 2):
            for sx in range(max(0, int(xs[j] + 0.5)), min(SW, int(xs[j + 1] + 0.5))):
                c[sy][sx] = max(c[sy][sx], val)


def ellipse(c, cx, cy, rx, ry, val=1.0):
    for sy in range(SH):
        for sx in range(SW):
            dx = (sx + 0.5) / SS - cx
            dy = (sy + 0.5) / SS - cy
            if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0:
                c[sy][sx] = max(c[sy][sx], val)


def ring(c, cx, cy, rx, ry, t, val=1.0):
    for sy in range(SH):
        for sx in range(SW):
            dx = (sx + 0.5) / SS - cx
            dy = (sy + 0.5) / SS - cy
            d = (dx / rx) ** 2 + (dy / ry) ** 2
            din = (dx / max(rx - t, 0.1)) ** 2 + (dy / max(ry - t, 0.1)) ** 2
            if d <= 1.0 and din >= 1.0:
                c[sy][sx] = max(c[sy][sx], val)


def line(c, x1, y1, x2, y2, t, val=1.0):
    """Thick line as a quad."""
    dx, dy = x2 - x1, y2 - y1
    ln = max((dx * dx + dy * dy) ** 0.5, 0.001)
    nx, ny = -dy / ln * t / 2, dx / ln * t / 2
    poly(c, [(x1 + nx, y1 + ny), (x2 + nx, y2 + ny),
             (x2 - nx, y2 - ny), (x1 - nx, y1 - ny)], val)


def downsample(c):
    out = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            s = 0.0
            for sy in range(SS):
                for sx in range(SS):
                    s += c[y * SS + sy][x * SS + sx]
            out[y][x] = min(255, int(s / (SS * SS) * 255 + 0.5))
    return out


# ---------------- the fleet (all facing right) ----------------

def airliner():
    c = canvas()
    # fuselage
    poly(c, [(10, 12), (46, 12), (56, 14.5), (58, 16), (46, 18.5), (10, 18.5)])
    # cockpit hint: nose top slope
    poly(c, [(46, 12), (52, 13), (56, 14.5), (46, 14.5)])
    # tail fin
    poly(c, [(13, 12.5), (6, 3.5), (12, 3.5), (20, 12.5)])
    # tailplane
    poly(c, [(12, 14), (4, 12.5), (7, 15.5), (14, 16)])
    # wing (swept, dips below)
    poly(c, [(28, 15), (18, 23), (23, 23.5), (36, 15.5)])
    # engine pod under the wing
    ellipse(c, 29, 20.5, 5, 2.6)
    return c


def prop():
    c = canvas()
    poly(c, [(16, 14), (44, 13), (52, 15), (44, 18), (16, 18)])
    # cabin
    poly(c, [(28, 14), (40, 14), (44, 10.5), (32, 10.5)])
    # high wing
    poly(c, [(24, 10), (46, 10), (44, 12.5), (26, 12.5)])
    # fin
    poly(c, [(18, 14), (13, 6), (18.5, 6), (24, 14)])
    # gear + prop
    line(c, 30, 18, 30, 23, 1.6)
    ellipse(c, 30, 24, 2, 2)
    line(c, 44, 18, 44, 22.5, 1.6)
    ellipse(c, 44, 23.5, 2, 2)
    line(c, 53, 10, 53, 20, 1.6)
    return c


def heli():
    c = canvas()
    # cabin
    ellipse(c, 38, 16.5, 10, 5.5)
    # boom + tail
    poly(c, [(30, 14.5), (10, 15.5), (10, 17.5), (30, 18.5)])
    poly(c, [(12, 16), (7, 9), (11, 9), (15, 16)])
    ellipse(c, 8, 13, 2.6, 2.6)
    # rotor + mast
    line(c, 14, 6.5, 60, 6.5, 1.8)
    line(c, 38, 7, 38, 11.5, 2)
    # skids
    line(c, 28, 24.5, 50, 24.5, 1.5)
    line(c, 33, 21.5, 33, 24.5, 1.4)
    line(c, 45, 21.5, 45, 24.5, 1.4)
    return c


def fighter():
    c = canvas()
    # slim pointed fuselage
    poly(c, [(8, 15), (44, 13), (60, 15.5), (44, 18), (8, 17)])
    # canopy
    poly(c, [(36, 13.5), (46, 13.8), (42, 10.5), (37, 10.5)])
    # delta wing
    poly(c, [(22, 15), (14, 22.5), (20, 22.5), (34, 15.5)])
    # twin fin
    poly(c, [(12, 14.5), (7, 5.5), (12, 5.5), (18, 14.5)])
    # intake
    poly(c, [(30, 17.5), (38, 17.5), (38, 20), (30, 20)])
    return c


def glider():
    c = canvas()
    # slender fuselage
    poly(c, [(6, 16.5), (40, 14.5), (56, 16), (40, 17.5), (6, 17.8)])
    # canopy
    ellipse(c, 45, 14.2, 5, 2.2)
    # long thin wing
    poly(c, [(20, 12.8), (62, 11.4), (62, 12.8), (22, 14.6)])
    # T-tail
    line(c, 9, 16.5, 9, 8, 1.8)
    line(c, 4, 8, 15, 8, 1.8)
    return c


def balloon():
    c = canvas()
    ellipse(c, 32, 10.5, 10.5, 10)
    poly(c, [(24, 17), (40, 17), (36, 22), (28, 22)])
    line(c, 28.7, 21, 28.7, 24, 1)
    line(c, 35.3, 21, 35.3, 24, 1)
    poly(c, [(27, 24), (37, 24), (37, 27.6), (27, 27.6)])
    return c


def drone():
    c = canvas()
    # body
    poly(c, [(24, 15), (40, 15), (38, 20.5), (26, 20.5)])
    # arms
    line(c, 26, 16, 14, 13.5, 1.8)
    line(c, 38, 16, 50, 13.5, 1.8)
    # rotors (thin discs) + masts
    line(c, 14, 13, 14, 11, 1.6)
    line(c, 50, 13, 50, 11, 1.6)
    line(c, 6, 10.5, 22, 10.5, 1.6)
    line(c, 42, 10.5, 58, 10.5, 1.6)
    # camera
    ellipse(c, 32, 22.5, 2.4, 2.2)
    return c


FLEET = [
    ("side_plane", airliner), ("side_small", prop), ("side_heli", heli),
    ("side_mil", fighter), ("side_glider", glider),
    ("side_balloon", balloon), ("side_drone", drone),
]


def preview(imgs, path):
    pad, scale = 4, 3
    cols = 4
    rows = (len(imgs) + cols - 1) // cols
    pw = cols * (W * scale + pad) + pad
    ph = rows * (H * scale + pad) + pad
    px = [[(16, 28, 40)] * pw for _ in range(ph)]
    for i, (_, a) in enumerate(imgs):
        ox = pad + (i % cols) * (W * scale + pad)
        oy = pad + (i // cols) * (H * scale + pad)
        for y in range(H * scale):
            for x in range(W * scale):
                v = a[y // scale][x // scale]
                if v:
                    bg = px[oy + y][ox + x]
                    px[oy + y][ox + x] = tuple(
                        bg[k] + (255 - bg[k]) * v // 255 for k in range(3))
    raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in px)

    def ch(t, d):
        cc = t + d
        return struct.pack(">I", len(d)) + cc + struct.pack(">I", zlib.crc32(cc))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(ch(b"IHDR", struct.pack(">IIBBBBB", pw, ph, 8, 2, 0, 0, 0)))
        f.write(ch(b"IDAT", zlib.compress(raw, 6)))
        f.write(ch(b"IEND", b""))


def emit_c(imgs, path):
    o = ["/* Generated by tools/gen_sideviews.py - side-view silhouettes",
         " * for the ambient selection overlay. White + alpha (recolorable). */",
         '#include "lvgl.h"', ""]
    for name, a in imgs:
        o.append(f"static const uint8_t {name}_map[] = {{")
        for y in range(H):
            row = []
            for x in range(W):
                row += ["0xff, 0xff", f"0x{a[y][x]:02x}"]
            o.append("    " + ", ".join(row) + ",")
        o.append("};")
        o.append(f"const lv_img_dsc_t img_{name} = {{")
        o.append("    .header.always_zero = 0,")
        o.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
        o.append(f"    .header.w = {W},")
        o.append(f"    .header.h = {H},")
        o.append(f"    .data_size = {W * H * 3},")
        o.append(f"    .data = {name}_map,")
        o.append("};")
        o.append("")
    open(path, "w").write("\n".join(o))


imgs = [(n, downsample(f())) for n, f in FLEET]
if "--preview" in sys.argv:
    preview(imgs, "sideviews_preview.png")
    print("sideviews_preview.png")
else:
    emit_c(imgs, "../main/img_sideviews.c")
    print("main/img_sideviews.c")
