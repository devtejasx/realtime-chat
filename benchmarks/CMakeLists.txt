# ---------------------------------------------------------------------------
# Microbenchmarks (Google Benchmark).
#
# Built only when RTC_BUILD_BENCHMARKS=ON, so the default developer and CI builds
# stay fast — Google Benchmark is a dependency nobody needs to compile in order to
# run the tests.
#
# Always benchmark a Release build. A Debug build measures the absence of the
# optimiser, not the code, and will happily report figures an order of magnitude
# off.
# ---------------------------------------------------------------------------

add_executable(rtc_benchmarks
    bench_main.cpp
    bench_jwt.cpp
    bench_cache.cpp
    bench_protocol.cpp
    bench_event_bus.cpp
    bench_authorization.cpp
    bench_database.cpp
)

target_include_directories(rtc_benchmarks PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/tests"   # reuse the existing fakes
)

target_link_libraries(rtc_benchmarks
    PRIVATE
        rtc_core
        benchmark::benchmark
        rtc_warnings
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(WARNING
        "Benchmarks are being built as '${CMAKE_BUILD_TYPE}'. Results from a "
        "non-optimised build are meaningless — reconfigure with "
        "-DCMAKE_BUILD_TYPE=Release.")
endif()

# `ctest -L benchmark` runs a smoke pass with a minimal time budget, purely to
# prove the benchmarks still execute. It is not a performance gate: CI runners are
# far too noisy for that, and a flaky timing assertion is worse than none.
add_test(NAME benchmarks_smoke
         COMMAND rtc_benchmarks --benchmark_min_time=0.01s --benchmark_filter=.*)
set_tests_properties(benchmarks_smoke PROPERTIES LABELS "benchmark" TIMEOUT 300)
