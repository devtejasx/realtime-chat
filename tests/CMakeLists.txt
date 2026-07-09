# =============================================================================
# Test suite. One executable for now; we may split it per-layer (unit vs
# integration) once integration tests need a live Postgres in Milestone 13.
# =============================================================================
add_executable(chat_tests
  config_test.cc
)

# gtest_main provides main() for us; chat_lib is the exact library the
# server links, so tests run against production code.
target_link_libraries(chat_tests PRIVATE chat_lib GTest::gtest_main)
target_compile_options(chat_tests PRIVATE -Wall -Wextra -Wpedantic)

# Registers every TEST() with CTest individually, so `ctest` reports each
# case separately instead of one opaque "chat_tests" blob.
include(GoogleTest)
gtest_discover_tests(chat_tests)
