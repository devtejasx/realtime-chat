#include <string>

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/events/audit_log_subscriber.hpp"
#include "rtc/services/audit_service.hpp"
#include "support/fake_audit_log_repository.hpp"
#include "support/fake_user_repository.hpp"

namespace {

using rtc::events::DomainEvent;
using rtc::events::EventType;

struct AuditFixture {
    AuditFixture() : service(audit_repository, users) {
        rtc::repositories::NewUser actor;
        actor.username = "ada";
        actor.email = "ada@example.com";
        actor.password_hash = "hash";
        actor_id = users.create(actor).id;
    }

    rtc::testing::FakeAuditLogRepository audit_repository;
    rtc::testing::FakeUserRepository users;
    rtc::services::AuditService service;
    std::int64_t actor_id = 0;
};

// --- which events are auditable --------------------------------------------

TEST(AuditPolicy, RecordsSecurityRelevantEvents) {
    for (const EventType type : {EventType::kUserRegistered, EventType::kUserLoggedIn,
                                 EventType::kUserLoggedOut, EventType::kPasswordChanged,
                                 EventType::kProfileUpdated, EventType::kUserRoleChanged,
                                 EventType::kConversationCreated, EventType::kConversationDeleted,
                                 EventType::kMemberAdded, EventType::kMemberRemoved,
                                 EventType::kMessageDeleted, EventType::kAdminAction}) {
        EXPECT_TRUE(rtc::services::is_auditable(type)) << rtc::events::to_string(type);
    }
}

TEST(AuditPolicy, SkipsHighVolumeTraffic) {
    // Auditing these would bury the security-relevant entries and roughly double
    // the write load on the hottest paths.
    for (const EventType type : {EventType::kMessageSent, EventType::kMessageEdited,
                                 EventType::kUserOnline, EventType::kUserOffline,
                                 EventType::kReactionAdded, EventType::kReactionRemoved,
                                 EventType::kAttachmentUploaded,
                                 EventType::kNotificationCreated}) {
        EXPECT_FALSE(rtc::services::is_auditable(type)) << rtc::events::to_string(type);
    }
}

// --- recording -------------------------------------------------------------

TEST(AuditService, PersistsAnAuditableEvent) {
    AuditFixture fixture;
    const auto event = rtc::events::UserLoggedIn{
        .user_id = fixture.actor_id, .username = "ada",
        .ip = "203.0.113.7", .user_agent = "curl/8"}.to_event();

    EXPECT_TRUE(fixture.service.record(event));
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);

    const auto& row = fixture.audit_repository.rows.front();
    EXPECT_EQ(row.event_type, "user.logged_in");
    EXPECT_EQ(row.actor_id, fixture.actor_id);
    EXPECT_EQ(row.target_type, "user");
    EXPECT_EQ(row.target_id, std::to_string(fixture.actor_id));
    // ip / user_agent are lifted out of the payload into their own columns.
    EXPECT_EQ(row.ip, "203.0.113.7");
    EXPECT_EQ(row.user_agent, "curl/8");
}

TEST(AuditService, IgnoresANonAuditableEvent) {
    AuditFixture fixture;
    const auto event = rtc::events::MessageSent{
        .message_id = 1, .conversation_id = 2, .sender_id = fixture.actor_id}.to_event();

    EXPECT_FALSE(fixture.service.record(event));
    EXPECT_TRUE(fixture.audit_repository.rows.empty());
}

TEST(AuditService, IsIdempotentOnEventId) {
    // The publisher is at-least-once (background dispatch, potential redelivery),
    // so a repeat must not duplicate history.
    AuditFixture fixture;
    const auto event =
        rtc::events::PasswordChanged{.user_id = fixture.actor_id}.to_event();

    EXPECT_TRUE(fixture.service.record(event));
    EXPECT_FALSE(fixture.service.record(event));
    EXPECT_EQ(fixture.audit_repository.rows.size(), 1U);
}

TEST(AuditService, DenormalisesTheActorUsername) {
    // actor_id is nulled when a user is deleted; an audit trail that loses who
    // acted is worthless, so the name is stored alongside.
    AuditFixture fixture;
    fixture.service.record(
        rtc::events::PasswordChanged{.user_id = fixture.actor_id}.to_event());
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);
    EXPECT_EQ(fixture.audit_repository.rows.front().actor_username, "ada");
}

TEST(AuditService, StillRecordsWhenTheActorCannotBeResolved) {
    // A missing user must never cost us the audit row itself.
    AuditFixture fixture;
    const auto event = rtc::events::PasswordChanged{.user_id = 999999}.to_event();
    EXPECT_TRUE(fixture.service.record(event));
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);
    EXPECT_FALSE(fixture.audit_repository.rows.front().actor_username.has_value());
}

TEST(AuditService, DerivesTheTargetPerEventType) {
    AuditFixture fixture;

    fixture.service.record(rtc::events::MemberAdded{
        .conversation_id = 77, .member_id = 5, .actor_id = fixture.actor_id}.to_event());
    fixture.service.record(rtc::events::MessageDeleted{
        .message_id = 31, .conversation_id = 77, .actor_id = fixture.actor_id,
        .by_moderator = true}.to_event());
    fixture.service.record(rtc::events::AdminAction{
        .actor_id = fixture.actor_id, .action = "feature.toggle",
        .target_type = "feature", .target_id = "search"}.to_event());

    ASSERT_EQ(fixture.audit_repository.rows.size(), 3U);
    EXPECT_EQ(fixture.audit_repository.rows[0].target_type, "conversation");
    EXPECT_EQ(fixture.audit_repository.rows[0].target_id, "77");
    EXPECT_EQ(fixture.audit_repository.rows[1].target_type, "message");
    EXPECT_EQ(fixture.audit_repository.rows[1].target_id, "31");
    // An admin action names its own target.
    EXPECT_EQ(fixture.audit_repository.rows[2].target_type, "feature");
    EXPECT_EQ(fixture.audit_repository.rows[2].target_id, "search");
}

TEST(AuditService, PreservesTheModeratorFlagOnADeletion) {
    // Deleting someone else's message is the case a reviewer cares about.
    AuditFixture fixture;
    fixture.service.record(rtc::events::MessageDeleted{
        .message_id = 4, .conversation_id = 9, .actor_id = fixture.actor_id,
        .by_moderator = true}.to_event());
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);
    EXPECT_TRUE(fixture.audit_repository.rows.front().metadata.at("by_moderator").get<bool>());
}

TEST(AuditService, CarriesCorrelationIds) {
    AuditFixture fixture;
    auto event = rtc::events::PasswordChanged{.user_id = fixture.actor_id}.to_event();
    event.correlation_id = "req-abc";
    event.trace_id = "0af7651916cd43dd8448eb211c80319c";

    fixture.service.record(event);
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);
    EXPECT_EQ(fixture.audit_repository.rows.front().correlation_id, "req-abc");
    EXPECT_EQ(fixture.audit_repository.rows.front().trace_id,
              "0af7651916cd43dd8448eb211c80319c");
}

TEST(AuditService, RecordsAnExplicitAdminAction) {
    AuditFixture fixture;
    EXPECT_TRUE(fixture.service.record_admin_action(fixture.actor_id, "user.ban", "user", "42",
                                                    {{"reason", "spam"}}, "10.0.0.1", "curl"));
    ASSERT_EQ(fixture.audit_repository.rows.size(), 1U);
    const auto& row = fixture.audit_repository.rows.front();
    EXPECT_EQ(row.event_type, "admin.action");
    EXPECT_EQ(row.target_id, "42");
    EXPECT_EQ(row.metadata.at("action"), "user.ban");
    EXPECT_EQ(row.ip, "10.0.0.1");
}

// --- search ----------------------------------------------------------------

TEST(AuditService, SearchesAndCounts) {
    AuditFixture fixture;
    for (int i = 0; i < 3; ++i) {
        fixture.service.record(
            rtc::events::PasswordChanged{.user_id = fixture.actor_id}.to_event());
    }
    fixture.service.record(rtc::events::UserLoggedIn{.user_id = fixture.actor_id}.to_event());

    rtc::repositories::AuditLogFilter filter;
    filter.event_type = "user.password_changed";
    EXPECT_EQ(fixture.service.count(filter), 3);

    rtc::dto::Pagination page;
    page.limit = 2;
    EXPECT_EQ(fixture.service.search(filter, page).size(), 2U);
}

TEST(AuditService, SummarisesByEventType) {
    AuditFixture fixture;
    fixture.service.record(rtc::events::UserLoggedIn{.user_id = fixture.actor_id}.to_event());
    fixture.service.record(rtc::events::UserLoggedIn{.user_id = fixture.actor_id}.to_event());
    fixture.service.record(
        rtc::events::PasswordChanged{.user_id = fixture.actor_id}.to_event());

    const auto summary = fixture.service.summary({});
    EXPECT_EQ(summary.at("total"), 3);
    ASSERT_TRUE(summary.at("by_event_type").is_array());
    EXPECT_EQ(summary.at("by_event_type").size(), 2U);
}

TEST(AuditService, GetThrowsForAMissingRecord) {
    AuditFixture fixture;
    EXPECT_THROW((void) fixture.service.get(4242), rtc::errors::NotFoundException);
}

TEST(AuditService, ProjectsARecordWithAStableKeySet) {
    AuditFixture fixture;
    fixture.service.record(rtc::events::UserLoggedIn{.user_id = fixture.actor_id}.to_event());
    const auto json =
        rtc::services::AuditService::to_json(fixture.audit_repository.rows.front());

    // Optional fields are emitted as null rather than omitted, so clients can rely
    // on the key set.
    for (const char* key : {"id", "event_id", "event_type", "actor_id", "actor_username",
                            "target_type", "target_id", "ip", "user_agent", "correlation_id",
                            "trace_id", "metadata", "occurred_at", "created_at"}) {
        EXPECT_TRUE(json.contains(key)) << "missing " << key;
    }
}

// --- the bus subscriber ----------------------------------------------------

TEST(AuditLogSubscriber, FollowsTheAuditFeatureFlag) {
    AuditFixture fixture;
    rtc::features::FeatureFlags flags;
    rtc::events::AuditLogSubscriber subscriber(fixture.service, flags);

    EXPECT_TRUE(subscriber.interested_in(EventType::kUserLoggedIn));
    // Switching the flag off stops the writes without unsubscribing.
    flags.set(rtc::features::Feature::kAuditLog, false);
    EXPECT_FALSE(subscriber.interested_in(EventType::kUserLoggedIn));
}

TEST(AuditLogSubscriber, IgnoresNonAuditableTypesEvenWhenEnabled) {
    AuditFixture fixture;
    rtc::features::FeatureFlags flags;
    rtc::events::AuditLogSubscriber subscriber(fixture.service, flags);
    EXPECT_FALSE(subscriber.interested_in(EventType::kMessageSent));
}

TEST(AuditLogSubscriber, WritesThroughToTheService) {
    AuditFixture fixture;
    rtc::features::FeatureFlags flags;
    rtc::events::AuditLogSubscriber subscriber(fixture.service, flags);

    subscriber.handle(rtc::events::UserLoggedIn{.user_id = fixture.actor_id}.to_event());
    EXPECT_EQ(fixture.audit_repository.rows.size(), 1U);
}

}  // namespace
