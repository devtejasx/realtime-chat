#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::events {

// Domain events published by the service layer.
//
// These describe *what happened in the domain* and are distinct from the
// realtime wire events in rtc/realtime/events.hpp, which describe what is sent
// to a connected client. One domain event may produce zero or many wire events,
// an audit row, a broker message and a metric — that fan-out is exactly what the
// bus exists to decouple.
//
// Keep kCount last; it sizes the name registry.
enum class EventType : std::size_t {
    // Identity / account lifecycle
    kUserRegistered,
    kUserLoggedIn,
    kUserLoggedOut,
    kPasswordChanged,
    kProfileUpdated,
    kUserRoleChanged,
    // Presence
    kUserOnline,
    kUserOffline,
    // Conversations / groups
    kConversationCreated,
    kConversationDeleted,
    kMemberAdded,
    kMemberRemoved,
    // Messaging
    kMessageSent,
    kMessageEdited,
    kMessageDeleted,
    kReactionAdded,
    kReactionRemoved,
    // Attachments / notifications
    kAttachmentUploaded,
    kNotificationCreated,
    // Administration
    kAdminAction,
    kCount,
};

inline constexpr std::size_t kEventTypeCount = static_cast<std::size_t>(EventType::kCount);

// Stable dotted wire names. These appear in audit rows, broker subjects and
// admin APIs, so treat them as a public contract: add, never rename.
inline constexpr std::array<std::string_view, kEventTypeCount> kEventTypeNames{{
    "user.registered",
    "user.logged_in",
    "user.logged_out",
    "user.password_changed",
    "user.profile_updated",
    "user.role_changed",
    "presence.user_online",
    "presence.user_offline",
    "conversation.created",
    "conversation.deleted",
    "conversation.member_added",
    "conversation.member_removed",
    "message.sent",
    "message.edited",
    "message.deleted",
    "reaction.added",
    "reaction.removed",
    "attachment.uploaded",
    "notification.created",
    "admin.action",
}};

[[nodiscard]] constexpr std::string_view to_string(EventType type) noexcept {
    const auto index = static_cast<std::size_t>(type);
    return index < kEventTypeCount ? kEventTypeNames[index] : std::string_view{"unknown"};
}

// Resolves a wire name back to its EventType; nullopt when unrecognised.
[[nodiscard]] std::optional<EventType> parse_event_type(std::string_view name) noexcept;

// The envelope every subscriber receives.
//
// A single envelope type (tagged with EventType, payload as JSON) rather than a
// class hierarchy plus visitors: every consumer of this bus — the audit log, the
// message broker, metrics — ultimately needs the event *serialised*, so JSON is
// the natural common representation and a hierarchy would only add conversion
// layers. Type safety is preserved at the point that matters, the publish site,
// by the typed builders in event_types.hpp.
struct DomainEvent {
    EventType type = EventType::kAdminAction;
    std::string event_id;  // unique id, for de-duplication
    utils::TimePoint occurred_at{};
    std::optional<std::int64_t> actor_id;  // the user who caused it, if any
    std::string correlation_id;            // request id, ties event -> request
    std::string trace_id;                  // W3C trace id, ties event -> trace
    nlohmann::json payload = nlohmann::json::object();

    // Builds an event, stamping a fresh id and the current time, and adopting
    // the active trace id from the calling thread's tracing context when one is
    // present. That means an event published while handling a request is
    // automatically correlated with that request's trace, with no plumbing at
    // the call site.
    [[nodiscard]] static DomainEvent make(EventType type,
                                          nlohmann::json payload,
                                          std::optional<std::int64_t> actor_id = std::nullopt);

    [[nodiscard]] std::string_view name() const noexcept { return to_string(type); }

    // Canonical serialisation, used for broker messages and audit metadata.
    [[nodiscard]] nlohmann::json to_json() const;
};

}  // namespace rtc::events
