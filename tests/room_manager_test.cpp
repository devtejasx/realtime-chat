#include "rtc/realtime/room_manager.hpp"

#include <gtest/gtest.h>

#include <crow/websocket.h>

namespace {

using rtc::realtime::RoomManager;

crow::websocket::connection* fake_conn(std::uintptr_t value) {
    return reinterpret_cast<crow::websocket::connection*>(value);
}

TEST(RoomManagerTest, JoinAddsConnectionToRoom) {
    RoomManager rooms;
    auto* conn = fake_conn(0x1);
    rooms.join(100, conn);
    ASSERT_EQ(rooms.connections_in_room(100).size(), 1U);
    EXPECT_EQ(rooms.connections_in_room(100).front(), conn);
}

TEST(RoomManagerTest, JoinManySubscribesToAllRooms) {
    RoomManager rooms;
    auto* conn = fake_conn(0x1);
    rooms.join_many({1, 2, 3}, conn);
    EXPECT_EQ(rooms.room_count(), 3U);
    EXPECT_EQ(rooms.connections_in_room(2).size(), 1U);
}

TEST(RoomManagerTest, LeaveRemovesFromRoom) {
    RoomManager rooms;
    auto* conn = fake_conn(0x1);
    rooms.join(100, conn);
    rooms.leave(100, conn);
    EXPECT_TRUE(rooms.connections_in_room(100).empty());
    EXPECT_EQ(rooms.room_count(), 0U);
}

TEST(RoomManagerTest, LeaveAllRemovesFromEveryRoom) {
    RoomManager rooms;
    auto* conn = fake_conn(0x1);
    auto* other = fake_conn(0x2);
    rooms.join_many({1, 2, 3}, conn);
    rooms.join(1, other);

    rooms.leave_all(conn);
    EXPECT_EQ(rooms.connections_in_room(1).size(), 1U);  // `other` still present
    EXPECT_TRUE(rooms.connections_in_room(2).empty());
    EXPECT_TRUE(rooms.connections_in_room(3).empty());
}

TEST(RoomManagerTest, MultipleConnectionsShareRoom) {
    RoomManager rooms;
    rooms.join(5, fake_conn(0x1));
    rooms.join(5, fake_conn(0x2));
    EXPECT_EQ(rooms.connections_in_room(5).size(), 2U);
}

}  // namespace
