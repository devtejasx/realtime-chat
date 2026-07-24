#include "rtc/realtime/session_manager.hpp"

#include <gtest/gtest.h>

#include <crow/websocket.h>

namespace {

using rtc::realtime::SessionManager;

// The SessionManager only ever uses connection pointers as opaque keys (it
// never dereferences them), so synthetic addresses are safe for these tests.
crow::websocket::connection* fake_conn(std::uintptr_t value) {
    return reinterpret_cast<crow::websocket::connection*>(value);
}

TEST(SessionManagerTest, AddAndLookupByConnection) {
    SessionManager manager;
    auto* conn = fake_conn(0x1000);
    auto session = manager.add(conn, /*user_id=*/7, "alice");
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->user_id, 7);
    EXPECT_EQ(manager.get(conn)->username, "alice");
    EXPECT_EQ(manager.session_count(), 1U);
}

TEST(SessionManagerTest, MultipleSessionsPerUser) {
    SessionManager manager;
    manager.add(fake_conn(0x1), 7, "alice");
    manager.add(fake_conn(0x2), 7, "alice");
    EXPECT_EQ(manager.connections_for_user(7).size(), 2U);
}

TEST(SessionManagerTest, RemoveClearsBothIndexes) {
    SessionManager manager;
    auto* conn = fake_conn(0x1);
    manager.add(conn, 7, "alice");
    auto removed = manager.remove(conn);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->user_id, 7);
    EXPECT_EQ(manager.get(conn), nullptr);
    EXPECT_TRUE(manager.connections_for_user(7).empty());
    EXPECT_EQ(manager.session_count(), 0U);
}

TEST(SessionManagerTest, RemoveUnknownConnectionReturnsNull) {
    SessionManager manager;
    EXPECT_EQ(manager.remove(fake_conn(0xdead)), nullptr);
}

TEST(SessionManagerTest, TouchUpdatesActivity) {
    SessionManager manager;
    auto* conn = fake_conn(0x1);
    auto session = manager.add(conn, 7, "alice");
    manager.touch(conn, 123456);
    EXPECT_EQ(session->last_activity_ms.load(), 123456);
}

TEST(SessionManagerTest, SnapshotReflectsLiveSessions) {
    SessionManager manager;
    manager.add(fake_conn(0x1), 1, "a");
    manager.add(fake_conn(0x2), 2, "b");
    EXPECT_EQ(manager.snapshot().size(), 2U);
}

}  // namespace
