#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/events/domain_event.hpp"

namespace rtc::events {

// Typed event builders.
//
// The bus carries a JSON-payload envelope (see DomainEvent), which keeps
// subscribers uniform — but an untyped publish site is easy to get wrong: a
// misspelled key or an omitted field fails silently and only shows up later in
// an audit row nobody reads. These small structs put the compiler back in charge
// at the point of publication while leaving the transport untyped.
//
// Each type exposes `to_event()`. Producers write:
//
//     publisher.publish(events::MessageSent{
//         .message_id = stored.id,
//         .conversation_id = stored.conversation_id,
//         .sender_id = actor_id,
//         .recipient_ids = participants,
//     }.to_event());
//
// so a new required field becomes a compile error in every producer.

// ---------------------------------------------------------------------------
// Identity / account
// ---------------------------------------------------------------------------

struct UserRegistered {
    std::int64_t user_id = 0;
    std::string username;
    std::string email;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kUserRegistered,
                                 {{"user_id", user_id}, {"username", username}, {"email", email}},
                                 user_id);
    }
};

struct UserLoggedIn {
    std::int64_t user_id = 0;
    std::string username;
    std::string ip;
    std::string user_agent;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kUserLoggedIn,
            {{"user_id", user_id}, {"username", username}, {"ip", ip}, {"user_agent", user_agent}},
            user_id);
    }
};

struct UserLoggedOut {
    std::int64_t user_id = 0;
    std::string session_id;
    bool all_sessions = false;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kUserLoggedOut,
            {{"user_id", user_id}, {"session_id", session_id}, {"all_sessions", all_sessions}},
            user_id);
    }
};

struct PasswordChanged {
    std::int64_t user_id = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kPasswordChanged, {{"user_id", user_id}}, user_id);
    }
};

struct ProfileUpdated {
    std::int64_t user_id = 0;
    std::vector<std::string> changed_fields;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kProfileUpdated,
                                 {{"user_id", user_id}, {"changed_fields", changed_fields}},
                                 user_id);
    }
};

struct UserRoleChanged {
    std::int64_t user_id = 0;
    std::int64_t actor_id = 0;  // the admin who made the change
    std::string previous_role;
    std::string new_role;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kUserRoleChanged,
            {{"user_id", user_id}, {"previous_role", previous_role}, {"new_role", new_role}},
            actor_id);
    }
};

// ---------------------------------------------------------------------------
// Presence
// ---------------------------------------------------------------------------

struct UserOnline {
    std::int64_t user_id = 0;
    std::string username;
    std::size_t session_count = 0;  // concurrent connections after this one

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kUserOnline,
            {{"user_id", user_id}, {"username", username}, {"session_count", session_count}},
            user_id);
    }
};

struct UserOffline {
    std::int64_t user_id = 0;
    std::string username;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kUserOffline, {{"user_id", user_id}, {"username", username}}, user_id);
    }
};

// ---------------------------------------------------------------------------
// Conversations / groups
// ---------------------------------------------------------------------------

struct ConversationCreated {
    std::int64_t conversation_id = 0;
    std::int64_t actor_id = 0;
    bool is_group = false;
    std::vector<std::int64_t> participant_ids;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kConversationCreated,
                                 {{"conversation_id", conversation_id},
                                  {"is_group", is_group},
                                  {"participant_ids", participant_ids}},
                                 actor_id);
    }
};

struct ConversationDeleted {
    std::int64_t conversation_id = 0;
    std::int64_t actor_id = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kConversationDeleted, {{"conversation_id", conversation_id}}, actor_id);
    }
};

struct MemberAdded {
    std::int64_t conversation_id = 0;
    std::int64_t member_id = 0;
    std::int64_t actor_id = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kMemberAdded,
                                 {{"conversation_id", conversation_id}, {"member_id", member_id}},
                                 actor_id);
    }
};

struct MemberRemoved {
    std::int64_t conversation_id = 0;
    std::int64_t member_id = 0;
    std::int64_t actor_id = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kMemberRemoved,
                                 {{"conversation_id", conversation_id}, {"member_id", member_id}},
                                 actor_id);
    }
};

// ---------------------------------------------------------------------------
// Messaging
// ---------------------------------------------------------------------------

struct MessageSent {
    std::int64_t message_id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t sender_id = 0;
    std::vector<std::int64_t> recipient_ids;
    std::size_t content_length = 0;  // length only — message bodies never leave the domain

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kMessageSent,
                                 {{"message_id", message_id},
                                  {"conversation_id", conversation_id},
                                  {"sender_id", sender_id},
                                  {"recipient_ids", recipient_ids},
                                  {"content_length", content_length}},
                                 sender_id);
    }
};

struct MessageEdited {
    std::int64_t message_id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t actor_id = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kMessageEdited,
                                 {{"message_id", message_id}, {"conversation_id", conversation_id}},
                                 actor_id);
    }
};

struct MessageDeleted {
    std::int64_t message_id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t actor_id = 0;
    // True when a moderator/owner deleted someone else's message — the case a
    // reviewer actually cares about in the audit log.
    bool by_moderator = false;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kMessageDeleted,
                                 {{"message_id", message_id},
                                  {"conversation_id", conversation_id},
                                  {"by_moderator", by_moderator}},
                                 actor_id);
    }
};

struct ReactionAdded {
    std::int64_t message_id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t user_id = 0;
    std::string emoji;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kReactionAdded,
            {{"message_id", message_id}, {"conversation_id", conversation_id}, {"emoji", emoji}},
            user_id);
    }
};

struct ReactionRemoved {
    std::int64_t message_id = 0;
    std::int64_t conversation_id = 0;
    std::int64_t user_id = 0;
    std::string emoji;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kReactionRemoved,
            {{"message_id", message_id}, {"conversation_id", conversation_id}, {"emoji", emoji}},
            user_id);
    }
};

// ---------------------------------------------------------------------------
// Attachments / notifications
// ---------------------------------------------------------------------------

struct AttachmentUploaded {
    std::int64_t attachment_id = 0;
    std::int64_t owner_id = 0;
    std::string content_type;
    std::int64_t size_bytes = 0;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kAttachmentUploaded,
                                 {{"attachment_id", attachment_id},
                                  {"owner_id", owner_id},
                                  {"content_type", content_type},
                                  {"size_bytes", size_bytes}},
                                 owner_id);
    }
};

struct NotificationCreated {
    std::int64_t notification_id = 0;
    std::int64_t user_id = 0;   // recipient
    std::int64_t actor_id = 0;  // who triggered it
    std::string kind;

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(
            EventType::kNotificationCreated,
            {{"notification_id", notification_id}, {"user_id", user_id}, {"kind", kind}},
            actor_id);
    }
};

// ---------------------------------------------------------------------------
// Administration
// ---------------------------------------------------------------------------

struct AdminAction {
    std::int64_t actor_id = 0;
    std::string action;       // "user.ban", "feature.toggle", ...
    std::string target_type;  // "user", "conversation", "feature"
    std::string target_id;
    nlohmann::json details = nlohmann::json::object();

    [[nodiscard]] DomainEvent to_event() const {
        return DomainEvent::make(EventType::kAdminAction,
                                 {{"action", action},
                                  {"target_type", target_type},
                                  {"target_id", target_id},
                                  {"details", details}},
                                 actor_id);
    }
};

}  // namespace rtc::events
