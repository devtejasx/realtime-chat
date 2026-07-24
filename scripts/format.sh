#!/usr/bin/env bash
# Apply clang-format in place to all C++ sources and headers.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "!! clang-format not found on PATH" >&2
    exit 1
fi

find "${ROOT_DIR}/src" "${ROOT_DIR}/include" "${ROOT_DIR}/tests" \
    -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
    -print0 | xargs -0 clang-format -i

echo ">> Formatted all C++ sources."
