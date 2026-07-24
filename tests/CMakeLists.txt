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
