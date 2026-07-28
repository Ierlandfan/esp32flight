#!/bin/sh
# Generate the per-class map sprites (28x28, nose up, white, recolorable):
# img_heli, img_small, img_mil, img_glider. Same format as img_plane.
set -e
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)

# helicopter: fuselage + tail boom + crossed rotor blades
magick -size 28x28 xc:none -fill white \
    -draw "roundrectangle 11.4,7 16.6,19 2.6,2.6" \
    -draw "rectangle 13.1,18 14.9,25" \
    -draw "rectangle 10.5,24.2 17.5,25.8" \
    -stroke white -strokewidth 2 \
    -draw "line 5,4 23,22" -draw "line 23,4 5,22" \
    "$tmp/heli.png"

# light single-prop: straight wings, prop bar on the nose
magick -size 28x28 xc:none -fill white \
    -draw "rectangle 10,2.6 18,4.2" \
    -draw "path 'M 14,3 L 15.6,8 L 15.6,22 L 14,25 L 12.4,22 L 12.4,8 Z'" \
    -draw "roundrectangle 3,11 25,14.4 1.6,1.6" \
    -draw "roundrectangle 8.6,22.4 19.4,24.6 1.2,1.2" \
    "$tmp/small.png"

# military fast jet: swept delta
magick -size 28x28 xc:none -fill white \
    -draw "path 'M 14,1 L 16,7 L 25,19 L 25,21.4 L 16.2,17.8 L 16.2,22 L 19.6,25 L 19.6,26.6 L 14,25.2 L 8.4,26.6 L 8.4,25 L 11.8,22 L 11.8,17.8 L 3,21.4 L 3,19 L 12,7 Z'" \
    "$tmp/mil.png"

# glider and friends: very long slender wings, slim body
magick -size 28x28 xc:none -fill white \
    -draw "path 'M 14,2.5 L 15.2,8 L 15.2,22 L 14,25 L 12.8,22 L 12.8,8 Z'" \
    -draw "roundrectangle 0.6,10.6 27.4,13 1.2,1.2" \
    -draw "roundrectangle 10,23 18,24.8 1,1" \
    "$tmp/glider.png"

for n in heli small mil glider; do
    magick "$tmp/$n.png" -depth 8 "$tmp/$n.rgba"
    python3 - "$tmp/$n.rgba" "main/img_$n.c" "img_$n" <<'EOF'
import sys

W = H = 28
raw = open(sys.argv[1], "rb").read()
assert len(raw) == W * H * 4
name = sys.argv[3]

out = []
for i in range(W * H):
    r, g, b, a = raw[i*4:i*4+4]
    px = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    out += [px & 0xFF, px >> 8, a]

with open(sys.argv[2], "w") as f:
    f.write('#include "lvgl.h"\n\n')
    f.write(f"static const uint8_t {name}_map[] = {{\n")
    for i in range(0, len(out), 24):
        f.write("    " + ",".join(str(b) for b in out[i:i+24]) + ",\n")
    f.write("};\n\n")
    f.write(f"const lv_img_dsc_t {name} = {{\n")
    f.write("    .header.always_zero = 0,\n")
    f.write("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
    f.write(f"    .header.w = {W},\n")
    f.write(f"    .header.h = {H},\n")
    f.write(f"    .data_size = {len(out)},\n")
    f.write(f"    .data = {name}_map,\n")
    f.write("};\n")
print(sys.argv[2], "written")
EOF
done
rm -rf "$tmp"
