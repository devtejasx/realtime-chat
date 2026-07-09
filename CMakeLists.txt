# =============================================================================
# realtime-chat — root build definition
#
# Responsibilities of this file:
#   * Declare the project, language standard, and global compile options.
#   * Pull in third-party dependencies (see cmake/dependencies.cmake).
#   * Define the core library target (chat_lib) and the server executable.
#
# Design decision: almost all code lives in a static library (chat_lib) and
# the executable is a thin wrapper around it. This is the standard pattern
# that makes the code unit-testable — the test binary links the exact same
# library the server does, so tests exercise production code, not copies.
# =============================================================================
cmake_minimum_required(VERSION 3.24)

project(realtime_chat
  VERSION 0.1.0
  DESCRIPTION "Production-grade real-time chat backend in modern C++"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard: C++20, no compiler-specific extensions. REQUIRED makes
# configuration fail loudly on an old compiler instead of silently degrading.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json so clangd / clang-tidy understand the project.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CHAT_BUILD_TESTS "Build the GoogleTest test suite" ON)

# Third-party dependencies (Crow, Asio, GoogleTest) are fetched and wired up
# in a dedicated module to keep this file focused on our own targets.
include(cmake/dependencies.cmake)

# ---------------------------------------------------------------------------
# chat_lib — everything except main(). New sources are added here as the
# project grows (services, repositories, controllers, ...).
# ---------------------------------------------------------------------------
add_library(chat_lib STATIC
  src/application.cc
  src/config/config.cc
)

# Includes are rooted at the repository top so headers are written as
# `#include "src/config/config.h"` — unambiguous, grep-able, Google-style.
target_include_directories(chat_lib PUBLIC ${PROJECT_SOURCE_DIR})

# Bake the project version into the binary so /health can report it.
target_compile_definitions(chat_lib PUBLIC CHAT_VERSION="${PROJECT_VERSION}")

target_link_libraries(chat_lib PUBLIC crow)

# Warnings apply to OUR code only (PRIVATE); third-party headers are included
# as SYSTEM so they do not drown us in warnings we cannot fix.
target_compile_options(chat_lib PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# chat_server — the deployable artifact. Contains only main().
# ---------------------------------------------------------------------------
add_executable(chat_server src/main.cc)
target_link_libraries(chat_server PRIVATE chat_lib)
target_compile_options(chat_server PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CHAT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
