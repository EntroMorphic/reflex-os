#!/usr/bin/env bash
#
# Build the firmware with the real ESP-IDF toolchain.
#
# This is the only local check that speaks for the firmware build; everything
# cheaper is an approximation of it. Run it natively when an ESP-IDF export
# script is present, and fall back to the container image CI uses otherwise --
# the point of the gate is that it runs, not that it runs in a container.
#
# Usage: tools/idf_build.sh [build-dir]   (or: make idf-build)

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${1:-build}
IDF_IMAGE=${IDF_IMAGE:-espressif/idf:release-v5.5}
IDF_TARGET_=${IDF_TARGET_:-esp32c6}

if [ -n "${IDF_PATH:-}" ] && [ -f "${IDF_PATH:-/nonexistent}/export.sh" ]; then
    echo "idf-build: native ($IDF_PATH)"
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" >/dev/null 2>&1 || {
        echo "could not source $IDF_PATH/export.sh"; exit 1; }
    idf.py -B "$BUILD_DIR" set-target "$IDF_TARGET_" >/dev/null || exit 1
    idf.py -B "$BUILD_DIR" build || exit 1
else
    echo "idf-build: container ($IDF_IMAGE)"
    "$(dirname "$0")/require_docker.sh" \
        "idf-build (or export IDF_PATH to a local ESP-IDF checkout)" || exit 1
    docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e IDF_COMPONENT_MANAGER=0 \
        -v "$PWD":/work -w /work "$IDF_IMAGE" \
        bash -c ". \$IDF_PATH/export.sh >/dev/null 2>&1 && \
                 idf.py -B $BUILD_DIR set-target $IDF_TARGET_ >/dev/null && \
                 idf.py -B $BUILD_DIR build" || exit 1
fi
