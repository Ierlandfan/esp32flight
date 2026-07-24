#ifdef __ANDROID__

/* First-run asset extraction: the APK carries the device's /assets content
 * under appdata/; copy it to internal storage so the core's plain fopen
 * calls (via path_remap) can read and write it. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL.h>

void apk_paths_set_base(const char *dir);
void apk_http_set_cainfo(const char *path);

#define EXTRACT_MARKER "extracted-v1"

static void mkdirs_for(const char *file_path)
{
    char tmp[640];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0770);
            *p = '/';
        }
    }
}

static int copy_asset(const char *apk_path, const char *dst_path)
{
    SDL_RWops *src = SDL_RWFromFile(apk_path, "rb");
    if (src == NULL) {
        return -1;
    }
    mkdirs_for(dst_path);
    FILE *dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        SDL_RWclose(src);
        return -1;
    }
    char buf[16 * 1024];
    size_t n;
    while ((n = SDL_RWread(src, buf, 1, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, dst);
    }
    fclose(dst);
    SDL_RWclose(src);
    return 0;
}

void android_glue_init(void)
{
    const char *internal = SDL_AndroidGetInternalStoragePath();
    static char base[576];
    snprintf(base, sizeof(base), "%s/assets", internal);

    char marker[640];
    snprintf(marker, sizeof(marker), "%s/%s", base, EXTRACT_MARKER);
    struct stat st;
    if (stat(marker, &st) != 0) {
        SDL_Log("apkflight: extracting assets to %s", base);
        SDL_RWops *mf = SDL_RWFromFile("appdata/manifest.txt", "rb");
        if (mf != NULL) {
            Sint64 len = SDL_RWsize(mf);
            char *txt = malloc(len + 1);
            SDL_RWread(mf, txt, 1, len);
            txt[len] = '\0';
            SDL_RWclose(mf);
            int copied = 0;
            for (char *line = strtok(txt, "\n"); line != NULL;
                 line = strtok(NULL, "\n")) {
                if (line[0] == '\0') {
                    continue;
                }
                char apk_path[576], dst[640];
                snprintf(apk_path, sizeof(apk_path), "appdata/%s", line);
                snprintf(dst, sizeof(dst), "%s/%s", base, line);
                if (copy_asset(apk_path, dst) == 0) {
                    copied++;
                }
            }
            free(txt);
            SDL_Log("apkflight: %d assets extracted", copied);
            FILE *m = fopen(marker, "w");
            if (m != NULL) {
                fclose(m);
            }
        } else {
            SDL_Log("apkflight: no appdata/manifest.txt in APK");
        }
    }

    apk_paths_set_base(base);
    static char ca[640];
    snprintf(ca, sizeof(ca), "%s/cacert.pem", base);
    apk_http_set_cainfo(ca);
}

/* Relaunch the activity from scratch: this is what "Zapisz" (save) does -
 * settings are persisted to disk first, then a clean restart reloads them
 * and re-runs geolocation, matching the device's save-and-restart flow. */
#include <jni.h>

void android_restart(void)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    jclass activity_cls = (*env)->GetObjectClass(env, activity);

    jmethodID get_pm = (*env)->GetMethodID(env, activity_cls,
        "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jobject pm = (*env)->CallObjectMethod(env, activity, get_pm);

    jmethodID get_pkg = (*env)->GetMethodID(env, activity_cls,
        "getPackageName", "()Ljava/lang/String;");
    jstring pkg = (jstring)(*env)->CallObjectMethod(env, activity, get_pkg);

    jclass pm_cls = (*env)->GetObjectClass(env, pm);
    jmethodID get_intent = (*env)->GetMethodID(env, pm_cls,
        "getLaunchIntentForPackage",
        "(Ljava/lang/String;)Landroid/content/Intent;");
    jobject intent = (*env)->CallObjectMethod(env, pm, get_intent, pkg);

    jclass intent_cls = (*env)->GetObjectClass(env, intent);
    jmethodID add_flags = (*env)->GetMethodID(env, intent_cls,
        "addFlags", "(I)Landroid/content/Intent;");
    /* NEW_TASK | CLEAR_TASK */
    (*env)->CallObjectMethod(env, intent, add_flags, 0x10008000);

    jmethodID start = (*env)->GetMethodID(env, activity_cls,
        "startActivity", "(Landroid/content/Intent;)V");
    (*env)->CallVoidMethod(env, activity, start, intent);

    (*env)->DeleteLocalRef(env, activity_cls);
    exit(0);   /* old process ends; the new task is already starting */
}

#endif /* __ANDROID__ */
