#!/bin/sh
# Cross-compile mbedTLS + curl as static libs for Android.
# Usage: scripts/build_android_deps.sh [abi...]  (default: arm64-v8a armeabi-v7a)
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
: "${ANDROID_HOME:=$HOME/Library/Android/sdk}"
NDK="$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)"
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
CMAKE="$(command -v cmake || echo "$HOME/.espressif/tools/cmake"/*/CMake.app/Contents/bin/cmake)"
API=21

ABIS="${*:-arm64-v8a armeabi-v7a}"

for ABI in $ABIS; do
    OUT="$TP/prebuilt/$ABI"
    mkdir -p "$OUT"

    echo "=== mbedTLS ($ABI) ==="
    B="$TP/build-mbedtls-$ABI"
    "$CMAKE" -S "$TP/mbedtls-3.6.4" -B "$B" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
        -DCMAKE_INSTALL_PREFIX="$OUT" >/dev/null
    "$CMAKE" --build "$B" -j8 >/dev/null
    "$CMAKE" --install "$B" >/dev/null

    echo "=== curl ($ABI) ==="
    B="$TP/build-curl-$ABI"
    "$CMAKE" -S "$TP/curl-8.14.1" -B "$B" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF \
        -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF -DCURL_USE_LIBPSL=OFF \
        -DCURL_USE_LIBSSH2=OFF -DUSE_NGHTTP2=OFF -DCURL_BROTLI=OFF \
        -DCURL_ZSTD=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_FTP=ON \
        -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_DICT=ON \
        -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_GOPHER=ON \
        -DCURL_DISABLE_IMAP=ON -DCURL_DISABLE_MQTT=ON \
        -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_RTSP=ON \
        -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_SMTP=ON \
        -DCURL_DISABLE_TFTP=ON -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none \
        -DMBEDTLS_INCLUDE_DIRS="$OUT/include" \
        -DMBEDTLS_LIBRARY="$OUT/lib/libmbedtls.a" \
        -DMBEDX509_LIBRARY="$OUT/lib/libmbedx509.a" \
        -DMBEDCRYPTO_LIBRARY="$OUT/lib/libmbedcrypto.a" \
        -DCMAKE_INSTALL_PREFIX="$OUT" >/dev/null
    "$CMAKE" --build "$B" -j8 >/dev/null
    "$CMAKE" --install "$B" >/dev/null

    echo "=== $ABI done ==="
    ls "$OUT/lib"
done
