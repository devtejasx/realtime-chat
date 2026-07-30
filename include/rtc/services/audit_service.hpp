#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/events/domain_event.hpp"
#include "rtc/models/audit_log.hpp"
#include "rtc/repositories/audit_log_repository.hpp"
#include "rtc/repositories/user_repository.hpp"

namespace rtc::services {

// Which domain events become audit records, and how each maps onto the audit
// row's target/metadata columns.
//
// Not every event is auditable: a message being sent is ordinary traffic, and
// recording all of them would bury the security-relevant entries in noise (and
// double the write volume of the busiest path in the system). What is recorded is
// the set a security review actually asks for — authentication, credential and
// profile changes, membership changes, deletions, role changes and admin actions.
[[nodiscard]] bool is_auditable(events::EventType type) noexcept;

// Reads and writes the audit trail.
//
// Writes arrive from the event bus via AuditLogSubscriber, never from a request
// handler: an audit failure must not fail the user action that triggered it, and
// the action has already happened by the time the event is published.
class AuditService {
  public:
    AuditService(repositories::IAuditLogRepository& repository,
                 repositories::IUserRepository& users) noexcept
        : repository_(repository), users_(users) {}

    AuditService(const AuditService&) = delete;
    AuditService& operator=(const AuditService&) = delete;

    // Persists an auditable domain event. Returns false when the event is not
    // auditable or was already recorded (idempotent on event id).
    bool record(const events::DomainEvent& event);

    // Records an explicit administrative action, for paths where the actor's
    // request context (ip, user agent) is worth capturing alongside the event.
    bool record_admin_action(std::int64_t actor_id,
                             const std::string& action,
                             const std::string& target_type,
                             const std::string& target_id,
                             nlohmann::json details = nlohmann::json::object(),
                             std::optional<std::string> ip = std::nullopt,
                             std::optional<std::string> user_agent = std::nullopt);

    [[nodiscard]] std::vector<models::AuditLog> search(const repositories::AuditLogFilter& filter,
                                                       const dto::Pagination& page);

    [[nodiscard]] std::int64_t count(const repositories::AuditLogFilter& filter);

    [[nodiscard]] models::AuditLog get(std::int64_t id);

    // Event-type histogram over the filter window, for the admin dashboard.
    [[nodiscard]] nlohmann::json summary(const repositories::AuditLogFilter& filter);

    // Public projection of a record. Kept here (rather than in a DTO) because the
    // shape is only ever produced by this service.
    [[nodiscard]] static nlohmann::json to_json(const models::AuditLog& log);

  private:
    // Derives (target_type, target_id) from an event's payload so audit rows are
    // queryable by subject without every producer having to spell it out.
    struct Target {
        std::optional<std::string> type;
        std::optional<std::string> id;
    };
    [[nodiscard]] static Target target_of(const events::DomainEvent& event);

    // Best-effort username lookup for the denormalised actor_username column.
    // Never throws: a missing user must not prevent the record being written.
    [[nodiscard]] std::optional<std::string> username_of(std::optional<std::int64_t> user_id);

    repositories::IAuditLogRepository& repository_;
    repositories::IUserRepository& users_;
};

}  // namespace rtc::services
