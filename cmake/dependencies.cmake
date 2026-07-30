# ---------------------------------------------------------------------------
# dependencies.cmake
#
# Centralised third-party dependency resolution for realtime-chat.
#
# Strategy: prefer system / vcpkg packages via find_package where they exist
# (OpenSSL, PostgreSQL, libpqxx), and use FetchContent for header-first or
# CMake-native libraries (Asio, Crow, spdlog, nlohmann_json, jwt-cpp, bcrypt,
# GoogleTest). This keeps the build reproducible without requiring every
# dependency to be pre-installed on the host.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# CMake 4.0 removed compatibility with `cmake_minimum_required(VERSION <3.5)`.
# Some pinned third-party projects (notably Bcrypt.cpp) still declare an older
# minimum, which makes their sub-configure a hard error under CMake >= 4. Raising
# the floor for *dependency* configuration keeps this project buildable on both
# CMake 3.x and 4.x without patching vendored sources. Our own CMake files
# already require 3.24, so this only ever affects fetched subprojects.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0" AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING
        "Minimum CMake policy version accepted from third-party subprojects" FORCE)
endif()

# Pin every dependency to an explicit tag for reproducible builds.
set(RTC_DEP_ASIO_TAG        "asio-1-30-2")
set(RTC_DEP_CROW_TAG        "v1.2.0")
set(RTC_DEP_SPDLOG_TAG      "v1.14.1")
set(RTC_DEP_JSON_TAG        "v3.11.3")
set(RTC_DEP_JWTCPP_TAG      "v0.7.0")
set(RTC_DEP_BCRYPT_TAG      "master")
set(RTC_DEP_GTEST_TAG       "v1.14.0")
set(RTC_DEP_BENCHMARK_TAG   "v1.8.3")

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)          # required by jwt-cpp for HMAC signing

# PostgreSQL client library (libpq) — required transitively by libpqxx.
find_package(PostgreSQL REQUIRED)

# libpqxx: prefer an installed CMake package (vcpkg / distro), else fetch it.
find_package(libpqxx CONFIG QUIET)
if(NOT libpqxx_FOUND)
    message(STATUS "libpqxx not found via find_package; fetching from source")
    set(SKIP_BUILD_TEST ON CACHE BOOL "" FORCE)
    FetchContent_Declare(libpqxx
        GIT_REPOSITORY https://github.com/jtv/libpqxx.git
        GIT_TAG        7.9.2
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(libpqxx)
endif()

# ---------------------------------------------------------------------------
# Asio (standalone) — transport layer required by Crow.
# ---------------------------------------------------------------------------
FetchContent_Declare(asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        ${RTC_DEP_ASIO_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio SYSTEM INTERFACE "${asio_SOURCE_DIR}/asio/include")
# ASIO_STANDALONE only: do NOT define ASIO_NO_DEPRECATED here. That macro removes
# the `asio::io_service` typedef, which Crow v1.2.0 still references in
# crow/http_request.h (request::post/dispatch), so defining it breaks the build
# against the pinned Asio 1.30.x. Revisit when Crow drops the deprecated alias.
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
target_link_libraries(asio INTERFACE Threads::Threads)
# Crow's bundled FindAsio.cmake locates headers through this variable.
set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include" CACHE PATH "" FORCE)

# ---------------------------------------------------------------------------
# nlohmann/json — JSON (de)serialisation for DTOs and error responses.
# ---------------------------------------------------------------------------
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${RTC_DEP_JSON_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# spdlog — structured logging.
# ---------------------------------------------------------------------------
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        ${RTC_DEP_SPDLOG_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(spdlog)

# ---------------------------------------------------------------------------
# Crow — HTTP / routing framework.
# ---------------------------------------------------------------------------
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        ${RTC_DEP_CROW_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(crow)

# ---------------------------------------------------------------------------
# jwt-cpp — JWT creation and verification (header-only, uses OpenSSL).
# ---------------------------------------------------------------------------
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JWT_DISABLE_PICOJSON OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG        ${RTC_DEP_JWTCPP_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(jwt-cpp)

# ---------------------------------------------------------------------------
# Bcrypt.cpp — password hashing (OpenBSD bcrypt implementation).
# Provides the `bcrypt` static library target and <bcrypt.h>.
# ---------------------------------------------------------------------------
FetchContent_Declare(bcrypt
    GIT_REPOSITORY https://github.com/hilch/Bcrypt.cpp.git
    GIT_TAG        ${RTC_DEP_BCRYPT_TAG}
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(bcrypt)

# ---------------------------------------------------------------------------
# GoogleTest — unit / integration testing.
# ---------------------------------------------------------------------------
if(RTC_BUILD_TESTS OR RTC_BUILD_BENCHMARKS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        ${RTC_DEP_GTEST_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# Google Benchmark — microbenchmarks. Fetched only when RTC_BUILD_BENCHMARKS=ON so
# the default build stays lean.
#
# BENCHMARK_ENABLE_TESTING is forced off: benchmarking the benchmark library is not
# our concern, and its own test suite pulls in further dependencies.
# ---------------------------------------------------------------------------
if(RTC_BUILD_BENCHMARKS)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_USE_BUNDLED_GTEST OFF CACHE BOOL "" FORCE)
    # Google Benchmark compiles *itself* with -Werror by default, so a warning newly
    # introduced by a compiler we do not control becomes a hard build failure in a
    # dependency (GCC 15 flags an unhandled enum in its sysinfo.cc, for instance).
    # Our own code keeps its strict warnings via rtc_warnings; third-party warnings
    # are not ours to fix.
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        ${RTC_DEP_BENCHMARK_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(benchmark)
endif()

# ---------------------------------------------------------------------------
# Normalise third-party target names.
#
# Depending on whether a dependency was found on the system or fetched from
# source, its imported target may carry a namespace or not. Resolve each to a
# single canonical variable so the rest of the build references one name.
# ---------------------------------------------------------------------------
if(TARGET libpqxx::pqxx)
    set(RTC_PQXX_TARGET libpqxx::pqxx)
elseif(TARGET pqxx)
    set(RTC_PQXX_TARGET pqxx)
elseif(TARGET libpqxx)
    set(RTC_PQXX_TARGET libpqxx)
else()
    message(FATAL_ERROR "Could not resolve a libpqxx target")
endif()

if(TARGET Crow::Crow)
    set(RTC_CROW_TARGET Crow::Crow)
elseif(TARGET Crow)
    set(RTC_CROW_TARGET Crow)
else()
    message(FATAL_ERROR "Could not resolve a Crow target")
endif()

if(TARGET jwt-cpp::jwt-cpp)
    set(RTC_JWT_TARGET jwt-cpp::jwt-cpp)
elseif(TARGET jwt-cpp)
    set(RTC_JWT_TARGET jwt-cpp)
else()
    message(FATAL_ERROR "Could not resolve a jwt-cpp target")
endif()

message(STATUS "Resolved dependency targets: ${RTC_CROW_TARGET}, ${RTC_PQXX_TARGET}, "
               "${RTC_JWT_TARGET}, bcrypt, spdlog::spdlog, nlohmann_json::nlohmann_json")

# ---------------------------------------------------------------------------
# Optional: Redis (redis-plus-plus). Only resolved when RTC_WITH_REDIS=ON. We
# rely on an installed package (vcpkg/distro) here to avoid pulling hiredis into
# the default build; when absent, the in-memory cache store is used at runtime.
# ---------------------------------------------------------------------------
if(RTC_WITH_REDIS)
    find_package(redis++ CONFIG QUIET)
    if(NOT redis++_FOUND)
        message(WARNING "RTC_WITH_REDIS=ON but redis-plus-plus was not found; "
                        "the build will fall back to the in-memory cache store")
    endif()
endif()
