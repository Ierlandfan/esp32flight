#!/bin/sh
# Build assets-8mb/ for N8R8 boards: everything from assets/ except the
# logo set, which is cut to the airlines in tools/airlines_top.txt and
# palette-quantized. Flags and the world maps are quantized too. Target:
# fit the 0x360000 spiffs partition of partitions-8mb.csv with headroom.
set -e
cd "$(dirname "$0")/.."

rm -rf assets-8mb
mkdir -p assets-8mb/logos
for d in assets/*; do
    b=$(basename "$d")
    [ "$b" = "logos" ] && continue
    cp -R "$d" "assets-8mb/$b"
done

# curated logos, quantized to a 64-color palette (flat brand marks
# survive this untouched, size drops 3-4x)
kept=0
while read -r code; do
    [ -z "$code" ] && continue
    src="assets/logos/$code.png"
    [ -e "$src" ] || continue
    magick "$src" -colors 64 -strip -define png:compression-level=9 \
        PNG8:"assets-8mb/logos/$code.png" 2>/dev/null || cp "$src" "assets-8mb/logos/$code.png"
    kept=$((kept+1))
    [ "$kept" -ge "${TOP_N:-999}" ] && break
done < tools/airlines_top.txt

# flags and bundled world maps take the same quantization
for f in assets-8mb/flags/*.png assets-8mb/map/*.png; do
    magick "$f" -colors 64 -strip -define png:compression-level=9 PNG8:"$f" 2>/dev/null || true
done

# full index of the ONLINE logo set: the firmware only fetches codes it
# finds here, so unknown callsign prefixes never hit the network
cp assets/logos/index.txt assets-8mb/logos/index.txt

# refresh the manifest used by the app build's asset extraction
( cd assets-8mb && find . -type f | sed 's|^\./||' | grep -v '^manifest.txt$' > manifest.txt )

echo "kept $kept logos"
du -sk assets-8mb assets-8mb/logos
