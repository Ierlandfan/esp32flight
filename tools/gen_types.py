#!/usr/bin/env python3
"""Generate assets/types.tsv: ICAO type designator -> "MANUFACTURER Model".

Data is the factual ICAO DOC 8643 designator list, fetched from the
community mirror at github.com/rikgale/ICAOList (the official
www4.icao.int endpoint is unreliable). Sorted by designator; the device
loads it into PSRAM and uses it to fill in the aircraft model when the
data feed carries no `desc` field (only airplanes.live sends one)."""
import csv
import io
import os
import urllib.request

URL = "https://raw.githubusercontent.com/rikgale/ICAOList/main/ICAOList.csv"

raw = urllib.request.urlopen(URL, timeout=30).read().decode("utf-8")
rows = list(csv.reader(io.StringIO(raw)))

out = {}
for r in rows[1:]:
    if len(r) >= 4 and r[0].strip():
        # "MANUFACTURER, Model" -> "MANUFACTURER Model"
        out.setdefault(r[0].strip(), r[3].replace(", ", " ", 1).strip())

dst = os.path.join(os.path.dirname(__file__) or ".", "..", "assets", "types.tsv")
with open(dst, "w") as f:
    for k in sorted(out):
        f.write(f"{k}\t{out[k]}\n")
print(f"{len(out)} types -> assets/types.tsv")
