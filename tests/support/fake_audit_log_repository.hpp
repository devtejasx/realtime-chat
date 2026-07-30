#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rtc/repositories/audit_log_repository.hpp"

namespace rtc::testing {

// In-memory IAuditLogRepository for unit-testing the audit service without a
// database. Faithfully reproduces the two behaviours callers depend on:
// idempotency on event_id (the real table has a UNIQUE constraint and an
// ON CONFLICT DO NOTHING insert), and newest-first ordering.
class FakeAuditLogRepository final : public repositories::IAuditLogRepository {
public:
    bool append(const repositories::NewAuditLog& input) override {
        if (!seen_event_ids_.insert(input.event_id).second) {
            return false;  // duplicate; matches ON CONFLICT DO NOTHING
        }
        models::AuditLog row;
        row.id = ++next_id_;
        row.event_id = input.event_id;
        row.event_type = input.event_type;
        row.actor_id = input.actor_id;
        row.actor_username = input.actor_username;
        row.target_type = input.target_type;
        row.target_id = input.target_id;
        row.ip = input.ip;
        row.user_agent = input.user_agent;
        row.correlation_id = input.correlation_id;
        row.trace_id = input.trace_id;
        row.metadata = input.metadata;
        row.occurred_at = input.occurred_at;
        row.created_at = utils::now();
        rows.push_back(std::move(row));
        return true;
    }

    [[nodiscard]] std::vector<models::AuditLog> search(
        const repositories::AuditLogFilter& filter, const dto::Pagination& page) override {
        std::vector<models::AuditLog> matched;
        for (const auto& row : rows) {
            if (matches(filter, row)) {
                matched.push_back(row);
            }
        }
        // Newest first, as the SQL does.
        std::reverse(matched.begin(), matched.end());

        std::vector<models::AuditLog> out;
        const auto offset = static_cast<std::size_t>(page.offset);
        for (std::size_t i = offset; i < matched.size() && out.size() < static_cast<std::size_t>(
                                                                            page.limit);
             ++i) {
            out.push_back(matched[i]);
        }
        return out;
    }

    [[nodiscard]] std::int64_t count(const repositories::AuditLogFilter& filter) override {
        std::int64_t total = 0;
        for (const auto& row : rows) {
            if (matches(filter, row)) {
                ++total;
            }
        }
        return total;
    }

    [[nodiscard]] std::optional<models::AuditLog> find_by_id(std::int64_t id) override {
        for (const auto& row : rows) {
            if (row.id == id) {
                return row;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>> counts_by_type(
        const repositories::AuditLogFilter& filter) override {
        std::vector<std::pair<std::string, std::int64_t>> out;
        for (const auto& row : rows) {
            if (!matches(filter, row)) {
                continue;
            }
            const auto existing = std::find_if(
                out.begin(), out.end(),
                [&row](const auto& entry) { return entry.first == row.event_type; });
            if (existing == out.end()) {
                out.emplace_back(row.event_type, 1);
            } else {
                ++existing->second;
            }
        }
        return out;
    }

    std::vector<models::AuditLog> rows;

private:
    [[nodiscard]] static bool matches(const repositories::AuditLogFilter& filter,
                                      const models::AuditLog& row) {
        if (filter.actor_id && row.actor_id != filter.actor_id) return false;
        if (filter.event_type && row.event_type != *filter.event_type) return false;
        if (filter.target_type && row.target_type != filter.target_type) return false;
        if (filter.target_id && row.target_id != filter.target_id) return false;
        if (filter.correlation_id && row.correlation_id != filter.correlation_id) return false;
        return true;
    }

    std::int64_t next_id_ = 0;
    std::unordered_set<std::string> seen_event_ids_;
};

}  // namespace rtc::testing
