#!/bin/sh
# Build the Sunton ESP32-4827S043 (480x272) variant into build-sunton43/.
# Keeps its sdkconfig inside the build dir so it never fights the default
# variant over the project-root sdkconfig.
set -e
cd "$(dirname "$0")/.."
idf.py -B build-sunton43 \
       -DSDKCONFIG=build-sunton43/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.sunton43" \
       set-target esp32s3 build "$@"
