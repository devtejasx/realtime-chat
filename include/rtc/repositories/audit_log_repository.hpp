#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/audit_log.hpp"

namespace rtc::repositories {

// Parameters for appending an audit record. Mirrors models::AuditLog minus the
// database-assigned fields.
struct NewAuditLog {
    std::string event_id;
    std::string event_type;
    std::optional<std::int64_t> actor_id;
    std::optional<std::string> actor_username;
    std::optional<std::string> target_type;
    std::optional<std::string> target_id;
    std::optional<std::string> ip;
    std::optional<std::string> user_agent;
    std::optional<std::string> correlation_id;
    std::optional<std::string> trace_id;
    nlohmann::json metadata = nlohmann::json::object();
    utils::TimePoint occurred_at{};
};

// Search criteria for the audit API. Every field is optional and ANDed; an empty
// filter returns the most recent records.
struct AuditLogFilter {
    std::optional<std::int64_t> actor_id;
    std::optional<std::string> event_type;
    std::optional<std::string> target_type;
    std::optional<std::string> target_id;
    std::optional<std::string> correlation_id;
    // Inclusive time window, as Unix epoch seconds.
    std::optional<std::int64_t> from_epoch;
    std::optional<std::int64_t> to_epoch;
};

// Persistence boundary for the audit log.
//
// Intentionally append-and-read only: no update, no delete. Retention is an
// operational concern handled by a scheduled partition drop or DELETE job (see
// docs/Monitoring.md), never by application code, so that no code path exists
// which could be abused to erase evidence.
class IAuditLogRepository {
  public:
    virtual ~IAuditLogRepository() = default;

    // Appends a record. Idempotent on event_id: a duplicate is silently ignored
    // (returns false) rather than throwing, because the writer is at-least-once
    // and a redelivery is normal, not an error.
    virtual bool append(const NewAuditLog& input) = 0;

    [[nodiscard]] virtual std::vector<models::AuditLog> search(const AuditLogFilter& filter,
                                                               const dto::Pagination& page) = 0;

    // Total matching rows, for the paginated response envelope.
    [[nodiscard]] virtual std::int64_t count(const AuditLogFilter& filter) = 0;

    [[nodiscard]] virtual std::optional<models::AuditLog> find_by_id(std::int64_t id) = 0;

    // Event-type histogram over the filter's window; powers the admin dashboard.
    [[nodiscard]] virtual std::vector<std::pair<std::string, std::int64_t>> counts_by_type(
        const AuditLogFilter& filter) = 0;
};

}  // namespace rtc::repositories
