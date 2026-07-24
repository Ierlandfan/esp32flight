#pragma once
/* Tiny standalone base64 encoder with the mbedTLS signature (the desktop
 * build has no mbedTLS; on Android the real header may win - both fine). */
#include <stddef.h>

static inline int mbedtls_base64_encode(unsigned char *dst, size_t dlen,
                                        size_t *olen, const unsigned char *src,
                                        size_t slen)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = 4 * ((slen + 2) / 3);
    if (dlen < need + 1) {
        return -1;
    }
    size_t o = 0;
    for (size_t i = 0; i < slen; i += 3) {
        unsigned v = src[i] << 16;
        if (i + 1 < slen) v |= src[i + 1] << 8;
        if (i + 2 < slen) v |= src[i + 2];
        dst[o++] = tbl[(v >> 18) & 63];
        dst[o++] = tbl[(v >> 12) & 63];
        dst[o++] = i + 1 < slen ? tbl[(v >> 6) & 63] : '=';
        dst[o++] = i + 2 < slen ? tbl[v & 63] : '=';
    }
    dst[o] = 0;
    *olen = o;
    return 0;
}
