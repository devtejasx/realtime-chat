#!/usr/bin/env bash
# Apply clang-format in place to all C++ sources and headers.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "!! clang-format not found on PATH" >&2
    exit 1
fi

# The CI check pins clang-format to this version (see .github/workflows/lint.yml).
# clang-format's output differs between major versions, so formatting with a
# different one produces a diff CI will reject. Warn rather than fail: a mismatched
# formatter is still better than none while working locally.
readonly EXPECTED_VERSION="20."
ACTUAL_VERSION="$(clang-format --version | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
case "$ACTUAL_VERSION" in
    "${EXPECTED_VERSION}"*) ;;
    *)
        echo "!! clang-format ${ACTUAL_VERSION} found; CI pins ${EXPECTED_VERSION}x." >&2
        echo "!! Formatting may differ from what CI accepts." >&2
        echo "!! Install the pinned version with: pipx install clang-format==20.1.8" >&2
        ;;
esac

find "${ROOT_DIR}/src" "${ROOT_DIR}/include" "${ROOT_DIR}/tests" "${ROOT_DIR}/benchmarks" \
    -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
    -print0 | xargs -0 clang-format -i

echo ">> Formatted all C++ sources with clang-format ${ACTUAL_VERSION}."
