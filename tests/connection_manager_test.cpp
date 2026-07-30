#include "rtc/realtime/connection_manager.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

namespace {

using rtc::realtime::ConnectionManager;

TEST(ConnectionManagerTest, EnvelopeWrapsTypeAndData) {
    const auto payload =
        ConnectionManager::make_envelope("message.created", nlohmann::json{{"id", 42}});
    const auto parsed = nlohmann::json::parse(payload);
    EXPECT_EQ(parsed["type"], "message.created");
    EXPECT_EQ(parsed["data"]["id"], 42);
}

TEST(ConnectionManagerTest, PublishToNobodyIsNoop) {
    ConnectionManager manager;
    // No sessions registered: publishing must not touch any connection.
    EXPECT_NO_THROW(manager.publish({1, 2, 3}, "presence.update", nlohmann::json::object()));
    EXPECT_EQ(manager.sessions().session_count(), 0U);
}

}  // namespace
