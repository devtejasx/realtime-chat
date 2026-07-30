#include "rtc/services/audit_service.hpp"

#include <exception>
#include <utility>

#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/utils/random.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::services {
namespace {

// Reads a payload field as a string regardless of its JSON type, so an integer
// id and a string id both land in the VARCHAR target_id column.
[[nodiscard]] std::optional<std::string> payload_id(const nlohmann::json& payload,
                                                    const char* key) {
    const auto it = payload.find(key);
    if (it == payload.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<std::int64_t>());
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> payload_string(const nlohmann::json& payload,
                                                        const char* key) {
    const auto it = payload.find(key);
    if (it == payload.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

}  // namespace

bool is_auditable(events::EventType type) noexcept {
    switch (type) {
        // Authentication and credential lifecycle.
        case events::EventType::kUserRegistered:
        case events::EventType::kUserLoggedIn:
        case events::EventType::kUserLoggedOut:
        case events::EventType::kPasswordChanged:
        case events::EventType::kProfileUpdated:
        case events::EventType::kUserRoleChanged:
        // Group membership: who could see what, and when that changed.
        case events::EventType::kConversationCreated:
        case events::EventType::kConversationDeleted:
        case events::EventType::kMemberAdded:
        case events::EventType::kMemberRemoved:
        // Destructive content actions.
        case events::EventType::kMessageDeleted:
        // Anything an administrator did.
        case events::EventType::kAdminAction:
            return true;

        // Deliberately not audited: ordinary, high-volume traffic. Recording
        // these would swamp the security-relevant entries and roughly double the
        // write load on the hottest paths.
        case events::EventType::kUserOnline:
        case events::EventType::kUserOffline:
        case events::EventType::kMessageSent:
        case events::EventType::kMessageEdited:
        case events::EventType::kReactionAdded:
        case events::EventType::kReactionRemoved:
        case events::EventType::kAttachmentUploaded:
        case events::EventType::kNotificationCreated:
        case events::EventType::kCount:
            return false;
    }
    return false;
}

AuditService::Target AuditService::target_of(const events::DomainEvent& event) {
    const nlohmann::json& payload = event.payload;
    Target target;
    switch (event.type) {
        case events::EventType::kUserRegistered:
        case events::EventType::kUserLoggedIn:
        case events::EventType::kUserLoggedOut:
        case events::EventType::kPasswordChanged:
        case events::EventType::kProfileUpdated:
        case events::EventType::kUserRoleChanged:
            target.type = "user";
            target.id = payload_id(payload, "user_id");
            break;
        case events::EventType::kConversationCreated:
        case events::EventType::kConversationDeleted:
        case events::EventType::kMemberAdded:
        case events::EventType::kMemberRemoved:
            target.type = "conversation";
            target.id = payload_id(payload, "conversation_id");
            break;
        case events::EventType::kMessageDeleted:
            target.type = "message";
            target.id = payload_id(payload, "message_id");
            break;
        case events::EventType::kAdminAction:
            // An admin action names its own target.
            target.type = payload_string(payload, "target_type");
            target.id = payload_string(payload, "target_id");
            break;
        default:
            break;
    }
    return target;
}

std::optional<std::string> AuditService::username_of(std::optional<std::int64_t> user_id) {
    if (!user_id) {
        return std::nullopt;
    }
    try {
        if (const auto user = users_.find_by_id(*user_id); user.has_value()) {
            return user->username;
        }
    } catch (const std::exception& ex) {
        // Denormalising the username is a convenience, not a requirement. Losing
        // it must never cost us the audit row itself.
        RTC_LOG_DEBUG("Audit: could not resolve username for user_id={}: {}", *user_id, ex.what());
    }
    return std::nullopt;
}

bool AuditService::record(const events::DomainEvent& event) {
    if (!is_auditable(event.type)) {
        return false;
    }

    const Target target = target_of(event);

    repositories::NewAuditLog row;
    row.event_id = event.event_id;
    row.event_type = std::string(event.name());
    row.actor_id = event.actor_id;
    row.actor_username = username_of(event.actor_id);
    row.target_type = target.type;
    row.target_id = target.id;
    // ip / user_agent live in the payload when the producer had a request context.
    row.ip = payload_string(event.payload, "ip");
    row.user_agent = payload_string(event.payload, "user_agent");
    row.correlation_id = event.correlation_id.empty()
                             ? std::nullopt
                             : std::optional<std::string>{event.correlation_id};
    row.trace_id =
        event.trace_id.empty() ? std::nullopt : std::optional<std::string>{event.trace_id};
    row.metadata = event.payload;
    row.occurred_at = event.occurred_at;

    const bool written = repository_.append(row);
    if (!written) {
        RTC_LOG_DEBUG("Audit record already present for event_id={}; skipped", event.event_id);
    }
    return written;
}

bool AuditService::record_admin_action(std::int64_t actor_id, const std::string& action,
                                       const std::string& target_type,
                                       const std::string& target_id, nlohmann::json details,
                                       std::optional<std::string> ip,
                                       std::optional<std::string> user_agent) {
    repositories::NewAuditLog row;
    row.event_id = utils::generate_hex_token(12);
    row.event_type = std::string(events::to_string(events::EventType::kAdminAction));
    row.actor_id = actor_id;
    row.actor_username = username_of(actor_id);
    row.target_type = target_type;
    row.target_id = target_id;
    row.ip = std::move(ip);
    row.user_agent = std::move(user_agent);
    row.metadata = nlohmann::json{{"action", action}, {"details", std::move(details)}};
    row.occurred_at = utils::now();
    return repository_.append(row);
}

std::vector<models::AuditLog> AuditService::search(const repositories::AuditLogFilter& filter,
                                                   const dto::Pagination& page) {
    return repository_.search(filter, page);
}

std::int64_t AuditService::count(const repositories::AuditLogFilter& filter) {
    return repository_.count(filter);
}

models::AuditLog AuditService::get(std::int64_t id) {
    auto log = repository_.find_by_id(id);
    if (!log) {
        throw errors::NotFoundException("Audit record not found", "id=" + std::to_string(id));
    }
    return *log;
}

nlohmann::json AuditService::summary(const repositories::AuditLogFilter& filter) {
    nlohmann::json by_type = nlohmann::json::array();
    std::int64_t total = 0;
    for (const auto& [type, count] : repository_.counts_by_type(filter)) {
        by_type.push_back({{"event_type", type}, {"count", count}});
        total += count;
    }
    return nlohmann::json{{"total", total}, {"by_event_type", std::move(by_type)}};
}

nlohmann::json AuditService::to_json(const models::AuditLog& log) {
    nlohmann::json out{
        {"id", log.id},
        {"event_id", log.event_id},
        {"event_type", log.event_type},
        {"metadata", log.metadata},
        {"occurred_at", utils::to_iso8601(log.occurred_at)},
        {"created_at", utils::to_iso8601(log.created_at)},
    };
    // Optional fields are emitted as null rather than omitted, so clients can
    // rely on a stable key set.
    out["actor_id"] = log.actor_id.has_value() ? nlohmann::json(*log.actor_id) : nlohmann::json();
    out["actor_username"] =
        log.actor_username.has_value() ? nlohmann::json(*log.actor_username) : nlohmann::json();
    out["target_type"] =
        log.target_type.has_value() ? nlohmann::json(*log.target_type) : nlohmann::json();
    out["target_id"] =
        log.target_id.has_value() ? nlohmann::json(*log.target_id) : nlohmann::json();
    out["ip"] = log.ip.has_value() ? nlohmann::json(*log.ip) : nlohmann::json();
    out["user_agent"] =
        log.user_agent.has_value() ? nlohmann::json(*log.user_agent) : nlohmann::json();
    out["correlation_id"] =
        log.correlation_id.has_value() ? nlohmann::json(*log.correlation_id) : nlohmann::json();
    out["trace_id"] = log.trace_id.has_value() ? nlohmann::json(*log.trace_id) : nlohmann::json();
    return out;
}

}  // namespace rtc::services
