#include "rtc/repositories/pg_audit_log_repository.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, event_id, event_type, actor_id, actor_username, target_type, target_id, ip, "
    "user_agent, correlation_id, trace_id, metadata::text AS metadata_text, "
    "EXTRACT(EPOCH FROM occurred_at)::bigint AS occurred_epoch, "
    "EXTRACT(EPOCH FROM created_at)::bigint  AS created_epoch";

[[nodiscard]] std::optional<std::string> read_opt_string(const pqxx::field& field) {
    if (field.is_null())
        return std::nullopt;
    return field.as<std::string>();
}

[[nodiscard]] std::optional<std::int64_t> read_opt_int(const pqxx::field& field) {
    if (field.is_null())
        return std::nullopt;
    return field.as<std::int64_t>();
}

[[nodiscard]] models::AuditLog map_audit_log(const pqxx::row& row) {
    models::AuditLog log;
    log.id = row["id"].as<std::int64_t>();
    log.event_id = row["event_id"].as<std::string>();
    log.event_type = row["event_type"].as<std::string>();
    log.actor_id = read_opt_int(row["actor_id"]);
    log.actor_username = read_opt_string(row["actor_username"]);
    log.target_type = read_opt_string(row["target_type"]);
    log.target_id = read_opt_string(row["target_id"]);
    log.ip = read_opt_string(row["ip"]);
    log.user_agent = read_opt_string(row["user_agent"]);
    log.correlation_id = read_opt_string(row["correlation_id"]);
    log.trace_id = read_opt_string(row["trace_id"]);
    // Metadata is read back as text and re-parsed. A malformed payload must not
    // take down an audit query, so parsing is non-throwing and degrades to {}.
    if (const auto text = read_opt_string(row["metadata_text"]); text.has_value()) {
        log.metadata = nlohmann::json::parse(*text, nullptr, /*allow_exceptions=*/false);
        if (log.metadata.is_discarded()) {
            log.metadata = nlohmann::json::object();
        }
    }
    log.occurred_at = Clock::from_time_t(row["occurred_epoch"].as<std::time_t>());
    log.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    return log;
}

// Every filtered statement shares this predicate block, expressed as
// "$n IS NULL OR <predicate>" so PostgreSQL can plan one fixed-shape statement
// regardless of which filters the caller supplied. Parameter order must match
// bind_filter() below.
constexpr const char* kFilterPredicate =
    " WHERE ($1::bigint IS NULL OR actor_id = $1)"
    "   AND ($2::text   IS NULL OR event_type = $2)"
    "   AND ($3::text   IS NULL OR target_type = $3)"
    "   AND ($4::text   IS NULL OR target_id = $4)"
    "   AND ($5::text   IS NULL OR correlation_id = $5)"
    "   AND ($6::bigint IS NULL OR occurred_at >= to_timestamp($6))"
    "   AND ($7::bigint IS NULL OR occurred_at <= to_timestamp($7))";

}  // namespace

bool PgAuditLogRepository::append(const NewAuditLog& input) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        // ON CONFLICT DO NOTHING makes the write idempotent on event_id: the
        // publisher is at-least-once, so a redelivery is expected traffic rather
        // than an error worth raising.
        const auto result = txn.exec_params(
            "INSERT INTO audit_logs (event_id, event_type, actor_id, actor_username, "
            "                        target_type, target_id, ip, user_agent, correlation_id, "
            "                        trace_id, metadata, occurred_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb, "
            "        COALESCE(to_timestamp($12), now())) "
            "ON CONFLICT (event_id) DO NOTHING "
            "RETURNING id",
            input.event_id,
            input.event_type,
            input.actor_id,
            input.actor_username,
            input.target_type,
            input.target_id,
            input.ip,
            input.user_agent,
            input.correlation_id,
            input.trace_id,
            input.metadata.dump(),
            input.occurred_at.time_since_epoch().count() == 0
                ? std::optional<std::int64_t>{}
                : std::optional<std::int64_t>{utils::to_unix_seconds(input.occurred_at)});
        return !result.empty();
    });
}

std::vector<models::AuditLog> PgAuditLogRepository::search(const AuditLogFilter& filter,
                                                           const dto::Pagination& page) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::AuditLog> {
        const std::string sql = std::string("SELECT ") + kColumns + " FROM audit_logs" +
                                kFilterPredicate +
                                " AND ($8::bigint IS NULL OR id < $8)"
                                " AND ($9::bigint IS NULL OR id > $9)"
                                " ORDER BY occurred_at DESC, id DESC LIMIT $10 OFFSET $11";
        const auto result = txn.exec_params(sql,
                                            filter.actor_id,
                                            filter.event_type,
                                            filter.target_type,
                                            filter.target_id,
                                            filter.correlation_id,
                                            filter.from_epoch,
                                            filter.to_epoch,
                                            page.before_id,
                                            page.after_id,
                                            page.limit,
                                            page.offset);

        std::vector<models::AuditLog> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.push_back(map_audit_log(row));
        }
        return out;
    });
}

std::int64_t PgAuditLogRepository::count(const AuditLogFilter& filter) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        const std::string sql =
            std::string("SELECT COUNT(*) AS total FROM audit_logs") + kFilterPredicate;
        const auto row = txn.exec_params1(sql,
                                          filter.actor_id,
                                          filter.event_type,
                                          filter.target_type,
                                          filter.target_id,
                                          filter.correlation_id,
                                          filter.from_epoch,
                                          filter.to_epoch);
        return row["total"].as<std::int64_t>();
    });
}

std::optional<models::AuditLog> PgAuditLogRepository::find_by_id(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::AuditLog> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns + " FROM audit_logs WHERE id = $1", id);
        if (result.empty()) {
            return std::nullopt;
        }
        return map_audit_log(result.front());
    });
}

std::vector<std::pair<std::string, std::int64_t>> PgAuditLogRepository::counts_by_type(
    const AuditLogFilter& filter) {
    using Row = std::pair<std::string, std::int64_t>;
    return with_transaction([&](pqxx::work& txn) -> std::vector<Row> {
        const std::string sql =
            std::string("SELECT event_type, COUNT(*) AS total FROM audit_logs") + kFilterPredicate +
            " GROUP BY event_type ORDER BY total DESC";
        const auto result = txn.exec_params(sql,
                                            filter.actor_id,
                                            filter.event_type,
                                            filter.target_type,
                                            filter.target_id,
                                            filter.correlation_id,
                                            filter.from_epoch,
                                            filter.to_epoch);
        std::vector<Row> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.emplace_back(row["event_type"].as<std::string>(), row["total"].as<std::int64_t>());
        }
        return out;
    });
}

}  // namespace rtc::repositories
