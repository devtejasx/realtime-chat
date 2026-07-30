cmake_minimum_required(VERSION 3.24)

project(realtime_chat
    VERSION 0.1.0
    DESCRIPTION "Production-grade real-time chat backend (Phase 1: Foundation & Authentication)"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    message(STATUS "No build type specified; defaulting to Release")
endif()

# Export compile commands for tooling (clang-tidy, IDEs, LSPs).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Place all binaries in a predictable location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Options
option(RTC_BUILD_TESTS "Build the GoogleTest suite" ON)
option(RTC_BUILD_BENCHMARKS "Build the Google Benchmark microbenchmarks" OFF)
option(RTC_ENABLE_WARNINGS "Enable a strict warning set" ON)
option(RTC_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(RTC_WITH_REDIS "Enable the Redis cache backend (requires redis-plus-plus)" OFF)
option(RTC_ENABLE_COVERAGE "Instrument for gcov/lcov coverage reporting (GCC/Clang, Debug)" OFF)

# Version defines available to the C++ sources.
add_compile_definitions(
    RTC_VERSION="${PROJECT_VERSION}"
    RTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}

    # spdlog compiles out any SPDLOG_LOGGER_* call below SPDLOG_ACTIVE_LEVEL, which
    # defaults to INFO. Without this, RTC_LOG_DEBUG / RTC_LOG_TRACE expand to
    # nothing and `LOG_LEVEL=debug` has no observable effect — debug logging was
    # unreachable in every build. Compiling all levels in and letting the runtime
    # level decide costs one atomic level comparison per suppressed call, which is
    # the right trade for being able to raise verbosity on a running service.
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

# ---------------------------------------------------------------------------
# Warning interface target — link against this to opt in to strict warnings.
# ---------------------------------------------------------------------------
add_library(rtc_warnings INTERFACE)
if(RTC_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(rtc_warnings INTERFACE /W4 /permissive-)
        if(RTC_WARNINGS_AS_ERRORS)
            target_compile_options(rtc_warnings INTERFACE /WX)
        endif()
    else()
        target_compile_options(rtc_warnings INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
            -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion)
        if(RTC_WARNINGS_AS_ERRORS)
            target_compile_options(rtc_warnings INTERFACE -Werror)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
include(cmake/dependencies.cmake)

# ---------------------------------------------------------------------------
# Project targets
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(RTC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RTC_BUILD_BENCHMARKS)
    if(NOT RTC_BUILD_TESTS)
        # Benchmarks reuse the fakes under tests/, and the smoke run is registered
        # with CTest — both need testing enabled.
        enable_testing()
    endif()
    add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Coverage instrumentation.
#
# Applied as a global option rather than to a single target: coverage is only
# meaningful when the library *and* the test binary are both instrumented, and
# gcov's counters live in the object files.
#
# Requires a Debug build — optimised code is inlined and reordered until line
# attribution becomes fiction, which is why an optimised coverage report reads as
# implausibly patchy.
# ---------------------------------------------------------------------------
if(RTC_ENABLE_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "RTC_ENABLE_COVERAGE requires GCC or Clang (gcov/llvm-cov).")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING
            "Coverage is enabled with CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}. Optimised "
            "builds produce misleading line attribution; use -DCMAKE_BUILD_TYPE=Debug.")
    endif()
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage -fno-inline)
    add_link_options(--coverage)
    message(STATUS "Coverage instrumentation enabled (see scripts/coverage.sh)")
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "realtime-chat ${PROJECT_VERSION} configured")
message(STATUS "  Build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard    : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests     : ${RTC_BUILD_TESTS}")
message(STATUS "  Strict warnings : ${RTC_ENABLE_WARNINGS}")
message(STATUS "")
