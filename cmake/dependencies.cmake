# =============================================================================
# Third-party dependencies, fetched at configure time with FetchContent.
#
# Why FetchContent instead of vcpkg/Conan for now:
#   * Zero setup for anyone cloning the repo — `cmake -B build` just works.
#   * Versions are pinned here, in-tree, and reviewed like any other change.
# The trade-off is configure-time downloads and no binary caching; when we
# add heavyweight compiled deps (libpqxx, OpenSSL) in Milestone 2 we will
# revisit this and take them from a package manager instead.
# =============================================================================
include(FetchContent)

# ---------------------------------------------------------------------------
# standalone Asio — the async I/O engine underneath Crow (header-only).
# The repository has no root CMakeLists.txt, so we wrap it in an INTERFACE
# target ourselves.
# ---------------------------------------------------------------------------
FetchContent_Declare(
  asio
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
# Standalone mode: no Boost dependency.
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)

find_package(Threads REQUIRED)
target_link_libraries(asio INTERFACE Threads::Threads)

if(WIN32)
  # Asio needs the WinSock API level declared and the socket libraries linked.
  # 0x0A00 = Windows 10+.
  target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
  target_link_libraries(asio INTERFACE ws2_32 mswsock)
endif()

# ---------------------------------------------------------------------------
# Crow — HTTP + WebSocket framework (header-only).
# SOURCE_SUBDIR points at a directory without a CMakeLists.txt on purpose:
# we consume Crow's headers directly instead of running its own build script,
# which would go looking for a system-installed Asio we don't have.
# ---------------------------------------------------------------------------
FetchContent_Declare(
  crow
  URL https://github.com/CrowCpp/Crow/archive/refs/tags/v1.2.1.2.tar.gz
  SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(crow)

add_library(crow INTERFACE)
target_include_directories(crow SYSTEM INTERFACE ${crow_SOURCE_DIR}/include)
target_link_libraries(crow INTERFACE asio)

# ---------------------------------------------------------------------------
# GoogleTest — only fetched when tests are enabled.
# ---------------------------------------------------------------------------
if(CHAT_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  )
  # On MSVC this keeps gtest's CRT choice consistent with ours; harmless
  # elsewhere and one less landmine if the project is built with cl.exe.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
