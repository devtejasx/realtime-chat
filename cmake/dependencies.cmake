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

# Pin every dependency to an explicit tag for reproducible builds.
set(RTC_DEP_ASIO_TAG        "asio-1-30-2")
set(RTC_DEP_CROW_TAG        "v1.2.0")
set(RTC_DEP_SPDLOG_TAG      "v1.14.1")
set(RTC_DEP_JSON_TAG        "v3.11.3")
set(RTC_DEP_JWTCPP_TAG      "v0.7.0")
set(RTC_DEP_BCRYPT_TAG      "master")
set(RTC_DEP_GTEST_TAG       "v1.14.0")

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
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
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
if(RTC_BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        ${RTC_DEP_GTEST_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(googletest)
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
