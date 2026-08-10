#include "airports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "airports";

/* Compact index in PSRAM; full records stay on flash and are read on
 * demand. The old approach kept the whole 550 KB TSV resident, which the
 * 7B cannot afford next to its LVGL framebuffers - and a float index
 * also makes the nearest-airport scan cheaper than text parsing did.
 * TSV: ICAO \t IATA \t CITY \t CC \t LAT \t LON \t NAME */
typedef struct {
    char     icao[4];   /* not NUL-terminated; padded with '\0' */
    char     iata[3];
    float    lat, lon;
    uint32_t off;       /* line start in airports.tsv */
} apt_idx_t;

static apt_idx_t *s_idx;
static int s_count;

void airports_init(void)
{
    FILE *f = fopen("/assets/airports.tsv", "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "airports.tsv missing; falling back to online lookups");
        return;
    }
    int cap = 9500;
    s_idx = heap_caps_malloc((size_t)cap * sizeof(apt_idx_t), MALLOC_CAP_SPIRAM);
    if (s_idx == NULL) {
        fclose(f);
        return;
    }
    char line[256];
    uint32_t off = 0;
    while (fgets(line, sizeof(line), f) != NULL && s_count < cap) {
        size_t ll = strlen(line);
        /* ICAO \t IATA \t CITY \t CC \t LAT \t LON \t NAME */
        const char *fields[7] = { line };
        int n = 1;
        for (char *c = line; *c != '\0' && n < 7; c++) {
            if (*c == '\t') {
                fields[n++] = c + 1;
            }
        }
        if (n == 7) {
            apt_idx_t *e = &s_idx[s_count];
            memset(e, 0, sizeof(*e));
            for (int i = 0; i < 4 && fields[0][i] != '\t'; i++) {
                e->icao[i] = fields[0][i];
            }
            for (int i = 0; i < 3 && fields[1][i] != '\t'; i++) {
                e->iata[i] = fields[1][i];
            }
            e->lat = (float)atof(fields[4]);
            e->lon = (float)atof(fields[5]);
            e->off = off;
            s_count++;
        }
        off += (uint32_t)ll;
    }
    fclose(f);
    ESP_LOGI(TAG, "%d airports indexed (%u KB; records stay on flash)",
             s_count, (unsigned)(s_count * sizeof(apt_idx_t) / 1024));
}

bool airports_nearest(double lat, double lon, char icao_out[5])
{
    if (s_idx == NULL) {
        return false;
    }
    double best = 1e18;
    int bi = -1;
    for (int i = 0; i < s_count; i++) {
        double dy = ((double)s_idx[i].lat - lat) * 110.57;
        double dx = ((double)s_idx[i].lon - lon) * 111.32 * 0.64; /* rough, fine for ranking */
        double d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            bi = i;
        }
    }
    if (bi < 0) {
        return false;
    }
    memcpy(icao_out, s_idx[bi].icao, 4);
    icao_out[4] = '\0';
    return true;
}

/* Read one line off flash and fill the airport record. */
static bool load_record(const apt_idx_t *e, airport_t *ap)
{
    FILE *f = fopen("/assets/airports.tsv", "rb");
    if (f == NULL) {
        return false;
    }
    char line[256] = "";
    bool got = fseek(f, (long)e->off, SEEK_SET) == 0 &&
               fgets(line, sizeof(line), f) != NULL;
    fclose(f);
    if (!got) {
        return false;
    }

    const char *fields[7] = { line };
    int n = 1;
    for (const char *c = line; *c != '\0' && *c != '\n' && n < 7; c++) {
        if (*c == '\t') {
            fields[n++] = c + 1;
        }
    }
    if (n < 7) {
        return false;
    }
    size_t name_len = 0;
    {
        const char *e2 = fields[6];
        while (*e2 != '\0' && *e2 != '\n') {
            e2++;
        }
        name_len = (size_t)(e2 - fields[6]);
    }
    memset(ap, 0, sizeof(*ap));
    snprintf(ap->icao, sizeof(ap->icao), "%.4s", e->icao);
    size_t len;
    len = (size_t)(fields[2] - fields[1] - 1);
    snprintf(ap->iata, sizeof(ap->iata), "%.*s", (int)len, fields[1]);
    len = (size_t)(fields[3] - fields[2] - 1);
    snprintf(ap->city, sizeof(ap->city), "%.*s", (int)len, fields[2]);
    len = (size_t)(fields[4] - fields[3] - 1);
    snprintf(ap->country, sizeof(ap->country), "%.*s", (int)len, fields[3]);
    ap->lat = atof(fields[4]);
    ap->lon = atof(fields[5]);
    snprintf(ap->name, sizeof(ap->name), "%.*s", (int)name_len, fields[6]);
    return true;
}

bool airports_lookup(const char *icao, airport_t *ap)
{
    if (s_idx == NULL || icao == NULL || strlen(icao) != 4) {
        return false;
    }
    for (int i = 0; i < s_count; i++) {
        if (memcmp(s_idx[i].icao, icao, 4) == 0) {
            return load_record(&s_idx[i], ap);
        }
    }
    return false;
}

bool airports_lookup_any(const char *code, airport_t *ap)
{
    size_t cl = strlen(code);
    if (cl == 4) {
        return airports_lookup(code, ap);
    }
    if (cl != 3 || s_idx == NULL) {
        return false;
    }
    for (int i = 0; i < s_count; i++) {
        if (strncasecmp(s_idx[i].iata, code, 3) == 0) {
            return load_record(&s_idx[i], ap);
        }
    }
    return false;
}
