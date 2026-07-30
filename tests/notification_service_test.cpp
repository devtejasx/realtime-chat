#include "rtc/services/notification_service.hpp"

#include <gtest/gtest.h>

#include "rtc/dto/pagination.hpp"
#include "rtc/jobs/background_executor.hpp"
#include "rtc/metrics/metrics_registry.hpp"
#include "rtc/notifications/push_provider.hpp"
#include "support/fake_notification_repository.hpp"
#include "support/recording_broadcaster.hpp"

namespace {

class NotificationServiceTest : public ::testing::Test {
  protected:
    rtc::testing::FakeNotificationRepository repo_;
    rtc::testing::RecordingBroadcaster broadcaster_;
    rtc::notifications::NullPushProvider push_;
    rtc::jobs::BackgroundExecutor executor_{1};
    rtc::metrics::MetricsRegistry metrics_;
    rtc::services::NotificationService service_{repo_, broadcaster_, push_, executor_, metrics_};
};

TEST_F(NotificationServiceTest, CreatePersistsAndBroadcasts) {
    const auto notification = service_.create(
        42, rtc::models::NotificationType::kNewMessage, nlohmann::json{{"message_id", 1}});
    EXPECT_EQ(notification.user_id, 42);
    EXPECT_EQ(repo_.count(), 1U);
    ASSERT_EQ(broadcaster_.count(), 1U);
    EXPECT_EQ(broadcaster_.last().type, "notification");
    EXPECT_EQ(broadcaster_.last().user_ids.front(), 42);
}

TEST_F(NotificationServiceTest, UnreadCountAndMarkRead) {
    const auto a = service_.create(1, rtc::models::NotificationType::kMention, {});
    service_.create(1, rtc::models::NotificationType::kReactionAdded, {});
    EXPECT_EQ(service_.unread_count(1), 2);

    EXPECT_TRUE(service_.mark_read(a.id, 1));
    EXPECT_EQ(service_.unread_count(1), 1);
}

TEST_F(NotificationServiceTest, MarkAllRead) {
    service_.create(1, rtc::models::NotificationType::kMention, {});
    service_.create(1, rtc::models::NotificationType::kMention, {});
    EXPECT_EQ(service_.mark_all_read(1), 2);
    EXPECT_EQ(service_.unread_count(1), 0);
}

TEST_F(NotificationServiceTest, ListUnreadOnlyFilters) {
    const auto a = service_.create(1, rtc::models::NotificationType::kMention, {});
    service_.create(1, rtc::models::NotificationType::kMention, {});
    service_.mark_read(a.id, 1);
    EXPECT_EQ(service_.list(1, rtc::dto::Pagination{}, /*unread_only=*/true).size(), 1U);
    EXPECT_EQ(service_.list(1, rtc::dto::Pagination{}, /*unread_only=*/false).size(), 2U);
}

}  // namespace
