#!/bin/sh
# Build the Sunton ESP32-4827S043 (480x272) variant into build-sunton43r/.
# Keeps its sdkconfig inside the build dir so it never fights the default
# variant over the project-root sdkconfig.
set -e
cd "$(dirname "$0")/.."
idf.py -B build-sunton43r \
       -DSDKCONFIG=build-sunton43r/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.sunton43r" \
       set-target esp32s3 build "$@"
