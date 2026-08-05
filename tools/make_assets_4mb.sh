#!/bin/sh
# Build assets-4mb/ for the 4 MB flash class: everything from assets/
# except bundled logos - every logo comes from the online set on demand
# (the index stays so unknown codes never hit the network). Flags and
# world maps are palette-quantized like the 8 MB set. Target: fit the
# 0x1b0000 spiffs partition of partitions-4mb.csv with log headroom.
set -e
cd "$(dirname "$0")/.."

rm -rf assets-4mb
mkdir -p assets-4mb/logos
for d in assets/*; do
    b=$(basename "$d")
    [ "$b" = "logos" ] && continue
    cp -R "$d" "assets-4mb/$b"
done

# flags and bundled world maps take the same quantization as the 8 MB set
for f in assets-4mb/flags/*.png assets-4mb/map/*.png; do
    magick "$f" -colors 64 -strip -define png:compression-level=9 PNG8:"$f" 2>/dev/null || true
done

# full index of the ONLINE logo set: the firmware only fetches codes it
# finds here, so unknown callsign prefixes never hit the network
cp assets/logos/index.txt assets-4mb/logos/index.txt

# refresh the manifest used by the app build's asset extraction
( cd assets-4mb && find . -type f | sed 's|^\./||' | grep -v '^manifest.txt$' > manifest.txt )

du -sk assets-4mb
