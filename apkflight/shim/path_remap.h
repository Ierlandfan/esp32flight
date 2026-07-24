#pragma once

/* Force-included into every TU: the core opens files under the device
 * mount point "/assets/..."; on desktop/Android that maps into the app's
 * data directory. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

FILE *apk_fopen(const char *path, const char *mode);
int   apk_stat(const char *path, struct stat *st);
int   apk_unlink(const char *path);
int   apk_rename(const char *from, const char *to);

#ifndef APK_NO_PATH_REMAP
#define fopen(p, m)  apk_fopen(p, m)
#define stat(p, b)   apk_stat(p, b)
#define unlink(p)    apk_unlink(p)
#define rename(a, b) apk_rename(a, b)
#endif
