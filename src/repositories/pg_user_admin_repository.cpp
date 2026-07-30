#include "rtc/repositories/pg_user_admin_repository.hpp"

#include <chrono>
#include <cstddef>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, username, email, password_hash, display_name, bio, avatar_url, role, ban_reason, "
    "banned_by, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM updated_at)::bigint AS updated_epoch, "
    "EXTRACT(EPOCH FROM banned_at)::bigint  AS banned_epoch";

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

// Fails closed: an unrecognised stored role grants only kUser rights. The CHECK
// constraint in migration 0011 makes this unreachable in practice, but a
// defensive default is the correct posture for an authorisation lookup — a
// corrupted row must never escalate privilege.
[[nodiscard]] security::Role read_role(const pqxx::field& field) {
    if (field.is_null()) {
        return security::kDefaultRole;
    }
    const std::string raw = field.as<std::string>();
    if (const auto parsed = security::parse_role(raw); parsed.has_value()) {
        return *parsed;
    }
    RTC_LOG_WARN("Unrecognised role '{}' in users.role; treating as '{}'",
                 raw,
                 security::to_string(security::kDefaultRole));
    return security::kDefaultRole;
}

[[nodiscard]] AdminUserRecord map_record(const pqxx::row& row) {
    AdminUserRecord record;
    record.user.id = row["id"].as<std::int64_t>();
    record.user.username = row["username"].as<std::string>();
    record.user.email = row["email"].as<std::string>();
    record.user.password_hash = row["password_hash"].as<std::string>();
    record.user.display_name = read_opt_string(row["display_name"]);
    record.user.bio = read_opt_string(row["bio"]);
    record.user.avatar_url = read_opt_string(row["avatar_url"]);
    record.user.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    record.user.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());

    record.role = read_role(row["role"]);
    if (const auto banned = read_opt_int(row["banned_epoch"]); banned.has_value()) {
        record.banned_at = Clock::from_time_t(static_cast<std::time_t>(*banned));
    }
    record.ban_reason = read_opt_string(row["ban_reason"]);
    record.banned_by = read_opt_int(row["banned_by"]);
    return record;
}

// Shared filter predicate; parameter order must match every bind site below.
// The ILIKE term is wrapped in %...% by the caller so the pattern is a bound
// parameter, never string-concatenated SQL.
constexpr const char* kFilterPredicate =
    " WHERE ($1::text IS NULL OR (username ILIKE $1 OR email ILIKE $1 "
    "                             OR COALESCE(display_name, '') ILIKE $1))"
    "   AND ($2::text IS NULL OR role = $2)"
    "   AND ($3::boolean IS NULL OR ($3 = TRUE AND banned_at IS NOT NULL) "
    "                            OR ($3 = FALSE AND banned_at IS NULL))";

// Builds the ILIKE pattern for the free-text term.
[[nodiscard]] std::optional<std::string> like_pattern(const std::optional<std::string>& query) {
    if (!query || query->empty()) {
        return std::nullopt;
    }
    return "%" + *query + "%";
}

[[nodiscard]] std::optional<std::string> role_text(const std::optional<security::Role>& role) {
    if (!role) {
        return std::nullopt;
    }
    return std::string(security::to_string(*role));
}

}  // namespace

std::optional<security::Role> PgUserAdminRepository::find_role(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<security::Role> {
        const auto result = txn.exec_params("SELECT role FROM users WHERE id = $1", user_id);
        if (result.empty()) {
            return std::nullopt;
        }
        return read_role(result.front()["role"]);
    });
}

std::optional<bool> PgUserAdminRepository::is_banned(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<bool> {
        const auto result = txn.exec_params(
            "SELECT (banned_at IS NOT NULL) AS banned FROM users WHERE id = $1", user_id);
        if (result.empty()) {
            return std::nullopt;
        }
        return result.front()["banned"].as<bool>();
    });
}

std::optional<AdminUserRecord> PgUserAdminRepository::find(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<AdminUserRecord> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns + " FROM users WHERE id = $1", user_id);
        if (result.empty()) {
            return std::nullopt;
        }
        return map_record(result.front());
    });
}

std::vector<AdminUserRecord> PgUserAdminRepository::list(const AdminUserFilter& filter,
                                                         const dto::Pagination& page) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<AdminUserRecord> {
        const std::string sql = std::string("SELECT ") + kColumns + " FROM users" +
                                kFilterPredicate + " ORDER BY id DESC LIMIT $4 OFFSET $5";
        const auto result = txn.exec_params(sql,
                                            like_pattern(filter.query),
                                            role_text(filter.role),
                                            filter.banned,
                                            page.limit,
                                            page.offset);
        std::vector<AdminUserRecord> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.push_back(map_record(row));
        }
        return out;
    });
}

std::int64_t PgUserAdminRepository::count(const AdminUserFilter& filter) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        const std::string sql =
            std::string("SELECT COUNT(*) AS total FROM users") + kFilterPredicate;
        const auto row = txn.exec_params1(
            sql, like_pattern(filter.query), role_text(filter.role), filter.banned);
        return row["total"].as<std::int64_t>();
    });
}

security::Role PgUserAdminRepository::set_role(std::int64_t user_id, security::Role role) {
    return with_transaction([&](pqxx::work& txn) -> security::Role {
        // Capturing the previous role needs care: a subquery inside RETURNING
        // would read the same snapshot the UPDATE is writing, so its result is
        // not dependable. The documented idiom is to join against a subquery,
        // which PostgreSQL evaluates *before* the update — with FOR UPDATE
        // taking the row lock so a concurrent role change cannot interleave and
        // make the audit record lie about the prior value.
        const auto result = txn.exec_params(
            "UPDATE users AS u SET role = $2, updated_at = now() "
            "FROM (SELECT id, role FROM users WHERE id = $1 FOR UPDATE) AS previous "
            "WHERE u.id = previous.id "
            "RETURNING previous.role AS previous_role",
            user_id,
            std::string(security::to_string(role)));
        if (result.empty()) {
            throw rtc::errors::NotFoundException("User not found",
                                                 "user_id=" + std::to_string(user_id));
        }
        return read_role(result.front()["previous_role"]);
    });
}

void PgUserAdminRepository::set_banned(std::int64_t user_id,
                                       bool banned,
                                       std::optional<std::string> reason,
                                       std::optional<std::int64_t> actor_id) {
    with_transaction([&](pqxx::work& txn) -> void {
        // One statement covers both directions: banning stamps the timestamp,
        // reason and actor; reinstating clears all three.
        const auto result = txn.exec_params(
            "UPDATE users SET "
            "  banned_at  = CASE WHEN $2::boolean THEN now()   ELSE NULL END, "
            "  ban_reason = CASE WHEN $2::boolean THEN $3::text ELSE NULL END, "
            "  banned_by  = CASE WHEN $2::boolean THEN $4::bigint ELSE NULL END, "
            "  updated_at = now() "
            "WHERE id = $1 RETURNING id",
            user_id,
            banned,
            reason,
            actor_id);
        if (result.empty()) {
            throw rtc::errors::NotFoundException("User not found",
                                                 "user_id=" + std::to_string(user_id));
        }
    });
}

std::vector<std::pair<security::Role, std::int64_t>> PgUserAdminRepository::counts_by_role() {
    using Row = std::pair<security::Role, std::int64_t>;
    return with_transaction([&](pqxx::work& txn) -> std::vector<Row> {
        const auto result =
            txn.exec("SELECT role, COUNT(*) AS total FROM users GROUP BY role ORDER BY role");
        std::vector<Row> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.emplace_back(read_role(row["role"]), row["total"].as<std::int64_t>());
        }
        return out;
    });
}

}  // namespace rtc::repositories
