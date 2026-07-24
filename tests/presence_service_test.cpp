#include "rtc/services/presence_service.hpp"

#include <gtest/gtest.h>

namespace {

using rtc::services::PresenceService;

TEST(PresenceServiceTest, FirstConnectTransitionsOnline) {
    PresenceService presence;
    EXPECT_TRUE(presence.on_connect(1));   // offline -> online
    EXPECT_TRUE(presence.is_online(1));
    EXPECT_EQ(presence.online_count(), 1U);
}

TEST(PresenceServiceTest, AdditionalSessionsDoNotRetrigger) {
    PresenceService presence;
    EXPECT_TRUE(presence.on_connect(1));
    EXPECT_FALSE(presence.on_connect(1));  // second device, still online
    EXPECT_TRUE(presence.is_online(1));
}

TEST(PresenceServiceTest, OfflineOnlyWhenLastSessionCloses) {
    PresenceService presence;
    presence.on_connect(1);
    presence.on_connect(1);
    EXPECT_FALSE(presence.on_disconnect(1));  // one session remains
    EXPECT_TRUE(presence.is_online(1));
    EXPECT_TRUE(presence.on_disconnect(1));    // last session -> offline
    EXPECT_FALSE(presence.is_online(1));
}

TEST(PresenceServiceTest, LastSeenRecordedOnGoingOffline) {
    PresenceService presence;
    EXPECT_FALSE(presence.last_seen(1).has_value());
    presence.on_connect(1);
    EXPECT_FALSE(presence.last_seen(1).has_value());  // still online
    presence.on_disconnect(1);
    EXPECT_TRUE(presence.last_seen(1).has_value());
}

TEST(PresenceServiceTest, DisconnectUnknownUserIsNoop) {
    PresenceService presence;
    EXPECT_FALSE(presence.on_disconnect(42));
}

}  // namespace
