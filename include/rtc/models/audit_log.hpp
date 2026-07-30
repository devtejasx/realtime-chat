#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// A persisted audit record — the in-memory view of a row in `audit_logs`.
//
// Pure data holder, like every other model here. Audit rows are append-only:
// there is deliberately no update or delete path in the repository, because the
// value of an audit log is that it cannot be quietly rewritten.
struct AuditLog {
    std::int64_t id = 0;
    std::string event_id;    // unique; makes persistence idempotent
    std::string event_type;  // rtc::events::EventType wire name

    // actor_username is denormalised on purpose: actor_id is nulled if the user
    // is deleted, and an audit trail that loses the name of who acted is
    // worthless for the review it exists to support.
    std::optional<std::int64_t> actor_id;
    std::optional<std::string> actor_username;

    std::optional<std::string> target_type;
    std::optional<std::string> target_id;
    std::optional<std::string> ip;
    std::optional<std::string> user_agent;
    std::optional<std::string> correlation_id;
    std::optional<std::string> trace_id;

    nlohmann::json metadata = nlohmann::json::object();

    utils::TimePoint occurred_at{};  // when the action happened
    utils::TimePoint created_at{};   // when the record was written
};

}  // namespace rtc::models
