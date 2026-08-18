#!/usr/bin/env python3
"""Generate main/img_sideviews.c: side-view aircraft silhouettes for the
ambient selection overlay (LV_IMG_CF_TRUE_COLOR_ALPHA, white + alpha so
lv_img recolor works like the map sprites). Shapes are authored as vector
outlines in a 64x28 design space - closed Catmull-Rom splines with
optional sharp vertices - and rasterized 8x supersampled at every emitted
size, so each target gets true anti-aliased edges with no runtime
rescaling. Two sets are emitted: the 2x set (128x56) used on >=800x480
panels, and an exact-size _ds set for the 480x272 downscale boards
(displayed at zoom 256, i.e. 1:1). Pure stdlib. --preview writes a PNG
montage instead of the C file."""
import struct
import sys
import zlib

SS = 8                        # supersample factor

# per-render globals set by render(): output size and design->super scale
W = H = SW = SH = 0
SCLX = SCLY = 1.0


def canvas():
    return [[0.0] * SW for _ in range(SH)]


def flatten(pts, samples=16):
    """Closed Catmull-Rom spline through pts; (x, y, True) = sharp corner
    (control point doubled so the curve passes through with a cusp)."""
    ctrl = []
    for p in pts:
        xy = (p[0], p[1])
        ctrl.append(xy)
        if len(p) > 2 and p[2]:
            ctrl.append(xy)
    n = len(ctrl)
    out = []
    for i in range(n):
        p0 = ctrl[(i - 1) % n]
        p1 = ctrl[i]
        p2 = ctrl[(i + 1) % n]
        p3 = ctrl[(i + 2) % n]
        for s in range(samples):
            t = s / samples
            t2, t3 = t * t, t * t * t
            out.append((
                0.5 * ((2 * p1[0]) + (-p0[0] + p2[0]) * t +
                       (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 +
                       (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3),
                0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t +
                       (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 +
                       (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)))
    return out


def poly(c, pts, val=1.0):
    """Even-odd scanline fill; pts in design space."""
    p = [(x * SCLX, y * SCLY) for x, y in pts]
    ymin = max(0, int(min(q[1] for q in p)))
    ymax = min(SH, int(max(q[1] for q in p)) + 2)
    for sy in range(ymin, ymax):
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


def blob(c, pts, val=1.0):
    """Filled smooth outline (closed spline)."""
    poly(c, flatten(pts), val)


def ellipse(c, cx, cy, rx, ry, val=1.0):
    x0 = max(0, int((cx - rx) * SCLX) - 1)
    x1 = min(SW, int((cx + rx) * SCLX) + 2)
    y0 = max(0, int((cy - ry) * SCLY) - 1)
    y1 = min(SH, int((cy + ry) * SCLY) + 2)
    for sy in range(y0, y1):
        for sx in range(x0, x1):
            dx = (sx + 0.5) / SCLX - cx
            dy = (sy + 0.5) / SCLY - cy
            if (dx / rx) ** 2 + (dy / ry) ** 2 <= 1.0:
                c[sy][sx] = max(c[sy][sx], val)


def ring(c, cx, cy, rx, ry, t, val=1.0):
    x0 = max(0, int((cx - rx) * SCLX) - 1)
    x1 = min(SW, int((cx + rx) * SCLX) + 2)
    y0 = max(0, int((cy - ry) * SCLY) - 1)
    y1 = min(SH, int((cy + ry) * SCLY) + 2)
    for sy in range(y0, y1):
        for sx in range(x0, x1):
            dx = (sx + 0.5) / SCLX - cx
            dy = (sy + 0.5) / SCLY - cy
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


# ---------------- the fleet (all facing right, 64x28 design) ----------------

def airliner():
    """A320-ish: round nose, tapered tail cone, swept fin and wing,
    underwing nacelle."""
    c = canvas()
    # fin behind the fuselage top line
    blob(c, [(20.5, 12.8, True), (14.5, 6.5), (11.2, 3.6), (7.4, 3.4, True),
             (7.0, 4.4, True), (10.6, 12.9, True)])
    # tailplane
    blob(c, [(14.0, 13.4, True), (5.2, 12.1, True), (7.4, 15.0, True),
             (15.4, 15.4, True)])
    # fuselage: one smooth outline
    blob(c, [(12.0, 12.6), (28.0, 11.9), (44.0, 11.7), (53.0, 12.4),
             (57.5, 13.8), (58.6, 15.2), (56.5, 17.0), (50.0, 18.2),
             (38.0, 18.7), (22.0, 18.6), (13.0, 17.5), (9.6, 15.6, True)])
    # swept wing, dips below the belly
    blob(c, [(36.5, 15.0, True), (20.0, 22.4), (17.6, 23.2, True),
             (22.4, 23.5, True), (31.0, 18.6), (36.5, 16.0, True)])
    # engine nacelle: stub pylon + rounded capsule
    poly(c, [(29.0, 17.5), (32.5, 17.5), (32.0, 20.0), (29.5, 20.0)])
    blob(c, [(26.4, 18.8), (30.5, 18.4), (34.2, 18.8), (34.5, 19.9),
             (30.5, 21.2), (26.1, 19.9)])
    return c


def prop():
    """C172-ish: high wing, fixed gear, spinning prop disc."""
    c = canvas()
    # fin: broad, rounded top, gentle sweep
    blob(c, [(23.0, 14.4, True), (18.5, 8.0), (15.8, 5.8), (13.0, 6.2, True),
             (13.6, 9.4), (15.4, 14.8, True)])
    # fuselage incl. rear window, roof, windshield + cowl, one outline
    blob(c, [(14.5, 15.2, True), (27.0, 14.0), (33.0, 10.6), (40.5, 10.2),
             (45.5, 11.4), (50.0, 13.2), (52.8, 15.0, True), (50.0, 16.8),
             (42.0, 17.7), (30.0, 17.9), (17.0, 16.6), (14.5, 16.2, True)])
    # high wing: thin blade lying on the roof
    blob(c, [(22.0, 10.0, True), (46.0, 10.2, True), (44.6, 11.5, True),
             (23.4, 11.3, True)])
    # wing strut
    line(c, 33.0, 11.2, 38.0, 16.4, 1.2)
    # gear + wheels
    line(c, 32.0, 17.6, 32.0, 21.6, 1.4)
    ellipse(c, 32.0, 22.4, 1.5, 1.5)
    line(c, 47.5, 16.8, 47.5, 21.0, 1.4)
    ellipse(c, 47.5, 21.8, 1.5, 1.5)
    # spinner + prop disc
    ellipse(c, 53.2, 15.2, 1.6, 1.2)
    line(c, 54.4, 10.8, 54.4, 19.6, 0.9, 0.5)
    return c


def heli():
    """EC135-ish: teardrop cabin, tapered boom, fenestron."""
    c = canvas()
    # fin over the fenestron
    blob(c, [(13.0, 15.6, True), (9.6, 9.6), (11.8, 8.6), (15.6, 14.6, True)])
    # cabin + boom as one smooth body
    blob(c, [(37.0, 11.6), (43.5, 12.2), (48.0, 14.2), (49.8, 16.2),
             (47.5, 18.9), (42.0, 20.6), (35.0, 20.7), (29.5, 19.2),
             (12.0, 17.1), (8.8, 16.5, True), (8.8, 15.9, True),
             (12.0, 15.5), (29.0, 13.4)])
    # fenestron ring
    ring(c, 9.6, 13.0, 3.2, 3.2, 1.5)
    # main rotor: slight droop + mast
    blob(c, [(17.0, 7.8, True), (38.0, 6.8), (59.0, 7.8, True),
             (59.0, 8.6, True), (38.0, 7.9), (17.0, 8.6, True)])
    line(c, 38.0, 8.2, 38.0, 12.2, 2.0)
    # skids
    line(c, 27.5, 24.6, 51.5, 24.6, 1.4)
    line(c, 51.5, 24.6, 54.5, 22.6, 1.3)
    line(c, 33.0, 20.8, 32.2, 24.6, 1.3)
    line(c, 46.0, 20.8, 46.8, 24.6, 1.3)
    return c


def fighter():
    """F-16-ish: blended body with bubble canopy in one outline,
    tall swept fin, chin intake."""
    c = canvas()
    # fin: curved leading edge, sharp tip
    blob(c, [(24.0, 13.8, True), (17.0, 6.5), (13.4, 3.0), (10.2, 3.0, True),
             (10.6, 4.4), (13.6, 13.9, True)])
    # tailplane
    blob(c, [(14.0, 15.3, True), (5.6, 17.6, True), (10.8, 18.9, True),
             (18.4, 16.6, True)])
    # body: spine, bubble canopy, needle nose, chin intake, one outline
    blob(c, [(10.0, 14.6, True), (20.0, 13.6), (28.5, 12.2), (33.5, 10.8),
             (38.0, 10.7), (42.0, 12.6), (46.0, 13.8, True), (53.0, 14.5),
             (60.8, 15.4, True), (53.0, 16.4), (46.0, 17.2, True),
             (43.0, 18.2), (40.0, 19.8, True), (34.5, 19.8, True),
             (31.5, 18.3), (22.0, 18.1), (12.0, 17.7), (10.0, 17.3, True)])
    # nozzle
    blob(c, [(7.6, 15.1, True), (10.4, 14.8, True), (10.4, 17.2, True),
             (7.6, 16.7, True)])
    # wing blade
    blob(c, [(35.0, 16.2, True), (21.5, 21.4), (19.4, 22.1, True),
             (24.2, 22.4, True), (31.5, 18.5), (37.0, 17.0, True)])
    return c


def miltrans():
    """C-130-ish: deep body, blunt round nose, upswept aft ramp, big
    rounded fin, four props on a high wing."""
    c = canvas()
    # fin: tall, rounded top
    blob(c, [(19.5, 12.4, True), (14.0, 6.2), (10.4, 3.4), (7.0, 3.6, True),
             (7.6, 6.0), (10.4, 12.6, True)])
    # tailplane
    blob(c, [(12.6, 12.8, True), (3.8, 12.0, True), (5.8, 14.6, True),
             (14.6, 14.9, True)])
    # fuselage
    blob(c, [(14.0, 11.8), (28.0, 11.3, True), (48.0, 11.3, True),
             (53.5, 11.9), (56.6, 13.8), (56.9, 15.8), (54.5, 18.0),
             (48.0, 19.6), (36.0, 20.2, True), (26.0, 19.9), (14.5, 16.2),
             (12.2, 15.0, True)])
    # high wing: thin slab riding clear above the spine so the
    # engines read against the sky (icon licence, not blueprint)
    blob(c, [(22.5, 9.4, True), (47.5, 9.6, True), (46.0, 11.5, True),
             (24.0, 11.3, True)])
    # four turboprops slung under the wing + prop discs
    for ex in (27.0, 33.0, 39.0, 45.0):
        blob(c, [(ex - 2.2, 10.4), (ex + 2.2, 10.4), (ex + 2.6, 12.2),
                 (ex + 2.0, 14.0), (ex - 2.0, 14.0), (ex - 2.6, 12.2)])
        line(c, ex + 2.4, 7.9, ex + 2.4, 15.2, 1.2, 0.75)
    return c


def glider():
    """Slender sailplane: long thin wing, T-tail."""
    c = canvas()
    # fuselage with canopy hump
    blob(c, [(7.0, 15.7), (20.0, 14.7), (34.0, 13.6), (41.0, 12.6),
             (46.5, 12.9), (52.0, 14.3), (54.6, 15.3, True), (50.0, 16.4),
             (40.0, 17.0), (24.0, 17.0), (10.0, 16.4)])
    # long thin wing, slight upsweep to the tip
    blob(c, [(25.0, 13.4, True), (44.0, 12.0), (60.5, 10.7, True),
             (60.9, 11.7, True), (44.0, 13.4), (26.5, 15.0, True)])
    # T-tail
    line(c, 9.4, 15.8, 9.0, 8.2, 1.6)
    blob(c, [(4.2, 7.0, True), (15.0, 7.2, True), (14.0, 8.6, True),
             (5.2, 8.4, True)])
    return c


def balloon():
    """Hot-air balloon: smooth envelope, ropes, basket."""
    c = canvas()
    blob(c, [(32.0, 2.0), (39.5, 3.9), (44.3, 9.0), (43.6, 14.6),
             (39.5, 19.2), (35.6, 21.6, True), (28.4, 21.6, True),
             (24.5, 19.2), (20.4, 14.6), (19.7, 9.0), (24.5, 3.9)])
    line(c, 28.8, 21.2, 28.3, 24.4, 0.9)
    line(c, 35.2, 21.2, 35.7, 24.4, 0.9)
    blob(c, [(27.6, 24.2, True), (36.4, 24.2, True), (36.0, 27.6, True),
             (28.0, 27.6, True)])
    return c


def drone():
    """Quadcopter: rounded body, arms, rotor discs, camera gimbal."""
    c = canvas()
    # body: rounded slab
    blob(c, [(24.5, 15.8), (32.0, 15.0), (39.5, 15.8), (38.5, 19.4),
             (32.0, 20.2), (25.5, 19.4)])
    # arms out to the motor pods
    line(c, 27.0, 16.6, 15.0, 14.2, 1.6)
    line(c, 37.0, 16.6, 49.0, 14.2, 1.6)
    # motor pods + masts
    blob(c, [(13.4, 12.6), (16.6, 12.6), (16.4, 15.0), (13.6, 15.0)])
    blob(c, [(47.4, 12.6), (50.6, 12.6), (50.4, 15.0), (47.6, 15.0)])
    # rotor discs: clear air gap above the pods
    line(c, 8.0, 11.2, 22.0, 11.2, 1.2)
    line(c, 42.0, 11.2, 56.0, 11.2, 1.2)
    # camera gimbal under the nose
    ellipse(c, 35.5, 21.2, 2.1, 1.9)
    return c


def vane():
    """Small compass: ring with a needle inside (rotated to track)."""
    c = canvas()
    ring(c, 16, 16, 14, 14, 2.4)
    poly(c, [(16, 5), (12.2, 17.5), (19.8, 17.5)])
    poly(c, [(16, 27), (13.6, 16.5), (18.4, 16.5)])
    ellipse(c, 16, 16, 2.4, 2.4)
    return c


FLEET = [
    ("side_plane", airliner), ("side_small", prop), ("side_heli", heli),
    ("side_mil", fighter), ("side_miltrans", miltrans),
    ("side_glider", glider),
    ("side_balloon", balloon), ("side_drone", drone),
]


def render(fn, design_w, design_h, out_w, out_h):
    """Rasterize one shape at out_w x out_h with SS supersampling."""
    global W, H, SW, SH, SCLX, SCLY
    W, H = out_w, out_h
    SW, SH = W * SS, H * SS
    SCLX = SW / float(design_w)
    SCLY = SH / float(design_h)
    return downsample(fn())


def preview(imgs, path):
    pad, scale = 4, 3
    cols = 4
    cw = max(len(a[0]) for _, a in imgs) * scale + pad
    chh = max(len(a) for _, a in imgs) * scale + pad
    rows = (len(imgs) + cols - 1) // cols
    pw = cols * cw + pad
    ph = rows * chh + pad
    px = [[(16, 28, 40)] * pw for _ in range(ph)]
    for i, (_, a) in enumerate(imgs):
        ih, iw = len(a), len(a[0])
        ox = pad + (i % cols) * cw
        oy = pad + (i // cols) * chh
        for y in range(ih * scale):
            for x in range(iw * scale):
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
         " * for the ambient selection overlay. White + alpha (recolorable).",
         " * _ds variants are pre-rendered at the 480x272 boards' display",
         " * size so they draw 1:1 (zoom 256) with full anti-aliasing. */",
         '#include "lvgl.h"', ""]
    for name, a in imgs:
        ih, iw = len(a), len(a[0])
        o.append(f"static const uint8_t {name}_map[] = {{")
        for y in range(ih):
            row = []
            for x in range(iw):
                row += ["0xff, 0xff", f"0x{a[y][x]:02x}"]
            o.append("    " + ", ".join(row) + ",")
        o.append("};")
        o.append(f"const lv_img_dsc_t img_{name} = {{")
        o.append("    .header.always_zero = 0,")
        o.append("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
        o.append(f"    .header.w = {iw},")
        o.append(f"    .header.h = {ih},")
        o.append(f"    .data_size = {iw * ih * 3},")
        o.append(f"    .data = {name}_map,")
        o.append("};")
        o.append("")
    open(path, "w").write("\n".join(o))


imgs = []
for n, f in FLEET:
    imgs.append((n, render(f, 64, 28, 128, 56)))         # 2x for >=800x480
for n, f in FLEET:
    imgs.append((n + "_ds", render(f, 64, 28, 38, 17)))  # 480x272 exact size
imgs.append(("side_vane", render(vane, 32, 32, 32, 32)))
imgs.append(("side_vane_ds", render(vane, 32, 32, 19, 19)))

if "--preview" in sys.argv:
    preview(imgs, "sideviews_preview.png")
    print("sideviews_preview.png")
else:
    emit_c(imgs, "../main/img_sideviews.c")
    print("main/img_sideviews.c")
