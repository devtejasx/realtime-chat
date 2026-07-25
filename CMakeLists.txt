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
option(RTC_ENABLE_WARNINGS "Enable a strict warning set" ON)
option(RTC_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(RTC_WITH_REDIS "Enable the Redis cache backend (requires redis-plus-plus)" OFF)

# Version defines available to the C++ sources.
add_compile_definitions(
    RTC_VERSION="${PROJECT_VERSION}"
    RTC_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    RTC_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    RTC_VERSION_PATCH=${PROJECT_VERSION_PATCH}
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
