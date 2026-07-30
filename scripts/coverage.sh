#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Build with coverage instrumentation, run the test suite, and produce an HTML
# report plus an lcov tracefile.
#
#   ./scripts/coverage.sh              # build, test, report
#   ./scripts/coverage.sh --open       # ... and open the report
#
# Requires GCC or Clang plus lcov and genhtml. MSVC is not supported (use OpenCppCoverage).
# ---------------------------------------------------------------------------
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-coverage"
REPORT_DIR="${BUILD_DIR}/coverage"
TRACEFILE="${REPORT_DIR}/coverage.info"

OPEN_REPORT=0
for arg in "$@"; do
    case "$arg" in
        --open) OPEN_REPORT=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v lcov >/dev/null || { echo "lcov is required (apt install lcov)" >&2; exit 1; }
command -v genhtml >/dev/null || { echo "genhtml is required (apt install lcov)" >&2; exit 1; }

echo "==> Configuring an instrumented Debug build"
# Debug is not optional here. Optimised builds inline and reorder until line
# attribution becomes fiction, producing reports that look implausibly patchy.
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DRTC_BUILD_TESTS=ON \
    -DRTC_ENABLE_COVERAGE=ON \
    -DRTC_BUILD_BENCHMARKS=OFF

echo "==> Building"
cmake --build "$BUILD_DIR" --parallel

mkdir -p "$REPORT_DIR"

echo "==> Capturing a baseline"
# A zero-count baseline over every instrumented file, merged with the post-run
# data below. Without it, files no test touches at all are simply absent from the
# report — which inflates the percentage by hiding the worst-covered code.
lcov --capture --initial \
    --directory "$BUILD_DIR" \
    --output-file "${REPORT_DIR}/baseline.info" \
    --rc branch_coverage=1 \
    >/dev/null 2>&1

echo "==> Running tests"
# Keep going on failure: a coverage report from a partially failing suite is still
# useful, and the exit status is preserved and reported at the end.
TEST_STATUS=0
ctest --test-dir "$BUILD_DIR" --output-on-failure --label-exclude benchmark || TEST_STATUS=$?

echo "==> Capturing results"
lcov --capture \
    --directory "$BUILD_DIR" \
    --output-file "${REPORT_DIR}/run.info" \
    --rc branch_coverage=1 \
    >/dev/null 2>&1

lcov --add-tracefile "${REPORT_DIR}/baseline.info" \
     --add-tracefile "${REPORT_DIR}/run.info" \
     --output-file "$TRACEFILE" \
     --rc branch_coverage=1 \
     >/dev/null 2>&1

echo "==> Filtering"
# Strip everything that is not our own production code. Test code, fetched
# dependencies and system headers would otherwise dominate the totals — a report
# that says "92%" because it counted GoogleTest's headers tells you nothing.
lcov --remove "$TRACEFILE" \
    '/usr/*' \
    '*/_deps/*' \
    '*/tests/*' \
    '*/benchmarks/*' \
    '*/build*/*' \
    --output-file "$TRACEFILE" \
    --rc branch_coverage=1 \
    >/dev/null 2>&1

echo "==> Generating HTML"
genhtml "$TRACEFILE" \
    --output-directory "$REPORT_DIR/html" \
    --title "realtime-chat coverage" \
    --legend \
    --branch-coverage \
    --demangle-cpp \
    >/dev/null

echo
echo "=== Coverage summary ==="
lcov --summary "$TRACEFILE" --rc branch_coverage=1

echo
echo "HTML report : ${REPORT_DIR}/html/index.html"
echo "lcov file   : ${TRACEFILE}"

if [ "$OPEN_REPORT" -eq 1 ]; then
    if command -v xdg-open >/dev/null; then
        xdg-open "${REPORT_DIR}/html/index.html"
    elif command -v open >/dev/null; then
        open "${REPORT_DIR}/html/index.html"
    fi
fi

if [ "$TEST_STATUS" -ne 0 ]; then
    echo
    echo "WARNING: the test suite failed (exit ${TEST_STATUS}); coverage reflects a partial run." >&2
    exit "$TEST_STATUS"
fi
