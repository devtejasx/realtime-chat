#!/usr/bin/env bash
# Build the project (with tests) and run the full CTest suite.
#
# Set RTC_RUN_DB_TESTS=1 (with a reachable PostgreSQL) to include the database
# integration tests; otherwise they are skipped.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS+=(-G Ninja)
fi

echo ">> Configuring with tests enabled"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Debug -DRTC_BUILD_TESTS=ON

echo ">> Building"
cmake --build "${BUILD_DIR}" --parallel

echo ">> Running tests"
ctest --test-dir "${BUILD_DIR}" --output-on-failure
