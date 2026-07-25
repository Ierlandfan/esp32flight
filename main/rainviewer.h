#pragma once

/* RainViewer precipitation radar (free, no key): latest composite frame,
 * served as XYZ tiles that tilemap.c blends over the basemap. */

/* Full URL prefix of the newest frame, e.g.
 * "https://tilecache.rainviewer.com/v2/radar/1690000000". Cached for 10
 * minutes; returns "" while unavailable (offline, API down). Blocking. */
const char *rainviewer_frame_path(void);

/* Increments whenever the frame path changes; goes into tile cache keys so
 * stale rain never outlives its frame. */
int rainviewer_generation(void);
