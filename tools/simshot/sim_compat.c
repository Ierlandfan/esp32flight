/* glibc (pre-2.38) lacks the BSD strl* the core uses; macOS/Android have
 * them, so only this harness needs the fallback. */
#include <string.h>

#if !defined(__APPLE__) && !defined(__ANDROID__)

size_t strlcpy(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);
    if (n > 0) {
        size_t take = len >= n ? n - 1 : len;
        memcpy(dst, src, take);
        dst[take] = '\0';
    }
    return len;
}

size_t strlcat(char *dst, const char *src, size_t n)
{
    size_t dl = strlen(dst);
    if (dl >= n) {
        return n + strlen(src);
    }
    return dl + strlcpy(dst + dl, src, n - dl);
}
#endif
