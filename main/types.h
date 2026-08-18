#pragma once

#include <stdbool.h>
#include <stddef.h>

/* ICAO type designator -> "MANUFACTURER Model" from assets/types.tsv
 * (DOC 8643). Backfills aircraft whose data feed sends no `desc` field
 * (only airplanes.live does). */
void types_init(void);
bool types_lookup(const char *icao, char *out, size_t out_size);
