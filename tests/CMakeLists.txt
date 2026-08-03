# ---------------------------------------------------------------------------
# Test suite (GoogleTest).
#
# All tests compile into a single executable linked against rtc_core, so the
# production code is exercised directly. The `support/` directory holds shared
# fakes and helpers, included via the tests/ root on the include path.
# ---------------------------------------------------------------------------
include(GoogleTest)

add_executable(rtc_tests
    # Phase 1
    config_test.cpp
    validators_test.cpp
    password_hasher_test.cpp
    jwt_test.cpp
    user_service_test.cpp
    auth_service_test.cpp
    auth_api_test.cpp
    user_repository_test.cpp
    # Phase 2
    user_profile_test.cpp
    conversation_service_test.cpp
    message_service_test.cpp
    read_receipt_service_test.cpp
    presence_service_test.cpp
    session_manager_test.cpp
    room_manager_test.cpp
    connection_manager_test.cpp
    messaging_integration_test.cpp
    # Phase 3
    cache_store_test.cpp
    cache_service_test.cpp
    rate_limiter_test.cpp
    background_executor_test.cpp
    metrics_registry_test.cpp
    mime_test.cpp
    reaction_service_test.cpp
    notification_service_test.cpp
    session_service_test.cpp
    # Phase 5 — enterprise platform
    api_versioning_test.cpp
    rbac_test.cpp
    event_bus_test.cpp
    tracing_test.cpp
    ws_protocol_test.cpp
    feature_flags_test.cpp
    audit_service_test.cpp
    search_service_test.cpp
    openapi_spec_test.cpp
    # HTTP-level contract coverage. These drive requests through the real Crow
    # router and middleware stack (see api_http_integration_test.cpp for why a
    # unit test of the path parser was not enough).
    api_http_integration_test.cpp
    openapi_contract_test.cpp
    # Horizontal scaling: cross-instance state.
    distributed_presence_test.cpp
    cluster_broadcast_test.cpp
    # Observability: logs joinable to traces.
    log_correlation_test.cpp
    trace_propagation_test.cpp
)

target_include_directories(rtc_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rtc_tests
    PRIVATE
        rtc_core
        GTest::gtest
        GTest::gtest_main
)

# Register every TEST/TEST_F with CTest for `ctest` discovery.
gtest_discover_tests(rtc_tests
    PROPERTIES
        LABELS "unit"
    DISCOVERY_TIMEOUT 30)

# ---------------------------------------------------------------------------
# Live HTTP suite.
#
# Kept in its own executable rather than folded into rtc_tests, for two reasons:
#
#   1. These tests bind a real TCP port and start the server. That is a
#      materially different category from the in-process suite — slower, and
#      dependent on the machine's networking — so it earns its own CTest label
#      and can be excluded with `ctest -LE live`.
#   2. Linking a listening server into the main unit-test binary causes some
#      endpoint-protection policies (Windows Smart App Control / WDAC among
#      them) to block the freshly built executable outright, which takes the
#      *entire* suite offline on a developer machine rather than just the
#      network-dependent part of it.
#
# It covers what the in-process suite structurally cannot: Crow runs the global
# middleware chain from http_connection, so only a real connection exercises
# ApiVersionMiddleware.
# ---------------------------------------------------------------------------
add_executable(rtc_http_tests http_server_live_test.cpp)

target_include_directories(rtc_http_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rtc_http_tests
    PRIVATE
        rtc_core
        asio
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(rtc_http_tests
    PROPERTIES
        LABELS "live"
    DISCOVERY_TIMEOUT 60)
