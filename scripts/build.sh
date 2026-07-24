#!/usr/bin/env bash
# Configure and build the project in Release mode.
#
# Usage: scripts/build.sh [Debug|Release]   (default: Release)
set -euo pipefail

BUILD_TYPE="${1:-Release}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS+=(-G Ninja)
fi

echo ">> Configuring (${BUILD_TYPE})"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo ">> Building"
cmake --build "${BUILD_DIR}" --parallel

echo ">> Done. Binary: ${BUILD_DIR}/bin/realtime-chat"
