#include "rtc/repositories/pg_session_repository.hpp"

#include <chrono>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, user_id, refresh_token_hash, user_agent, ip, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM last_used_at)::bigint AS last_used_epoch, "
    "EXTRACT(EPOCH FROM expires_at)::bigint AS expires_epoch, "
    "EXTRACT(EPOCH FROM revoked_at)::bigint AS revoked_epoch";

[[nodiscard]] std::optional<std::string> opt_str(const pqxx::field& f) {
    return f.is_null() ? std::nullopt : std::optional<std::string>(f.as<std::string>());
}

[[nodiscard]] models::Session map_row(const pqxx::row& row) {
    models::Session s;
    s.id = row["id"].as<std::string>();
    s.user_id = row["user_id"].as<std::int64_t>();
    s.refresh_token_hash = row["refresh_token_hash"].as<std::string>();
    s.user_agent = opt_str(row["user_agent"]);
    s.ip = opt_str(row["ip"]);
    s.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    s.last_used_at = Clock::from_time_t(row["last_used_epoch"].as<std::time_t>());
    s.expires_at = Clock::from_time_t(row["expires_epoch"].as<std::time_t>());
    if (!row["revoked_epoch"].is_null()) {
        s.revoked_at = Clock::from_time_t(row["revoked_epoch"].as<std::time_t>());
    }
    return s;
}

}  // namespace

models::Session PgSessionRepository::create(const NewSession& input) {
    return with_transaction([&](pqxx::work& txn) -> models::Session {
        const std::string sql =
            std::string(
                "INSERT INTO sessions "
                "(id, user_id, refresh_token_hash, user_agent, ip, expires_at) "
                "VALUES ($1,$2,$3,$4,$5, now() + make_interval(secs => $6)) RETURNING ") +
            kColumns;
        return map_row(txn.exec_params(sql,
                                       input.id,
                                       input.user_id,
                                       input.refresh_token_hash,
                                       input.user_agent,
                                       input.ip,
                                       static_cast<double>(input.ttl_seconds))
                           .front());
    });
}

std::optional<models::Session> PgSessionRepository::find_by_id(std::string_view id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::Session> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns + " FROM sessions WHERE id = $1", std::string(id));
        if (result.empty())
            return std::nullopt;
        return map_row(result.front());
    });
}

std::vector<models::Session> PgSessionRepository::list_active_for_user(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Session> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns +
                " FROM sessions WHERE user_id = $1 AND revoked_at IS NULL AND expires_at > now() "
                "ORDER BY last_used_at DESC",
            user_id);
        std::vector<models::Session> out;
        out.reserve(result.size());
        for (const auto& row : result) {
            out.push_back(map_row(row));
        }
        return out;
    });
}

void PgSessionRepository::rotate(std::string_view id, std::string_view new_hash) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params(
            "UPDATE sessions SET refresh_token_hash = $2, last_used_at = now() WHERE id = $1",
            std::string(id),
            std::string(new_hash));
    });
}

bool PgSessionRepository::revoke(std::string_view id, std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        return txn.exec_params(
                      "UPDATE sessions SET revoked_at = now() "
                      "WHERE id = $1 AND user_id = $2 AND revoked_at IS NULL",
                      std::string(id),
                      user_id)
                   .affected_rows() > 0;
    });
}

std::int64_t PgSessionRepository::revoke_all(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        return static_cast<std::int64_t>(
            txn.exec_params("UPDATE sessions SET revoked_at = now() "
                            "WHERE user_id = $1 AND revoked_at IS NULL",
                            user_id)
                .affected_rows());
    });
}

std::int64_t PgSessionRepository::revoke_all_except(std::int64_t user_id,
                                                    std::string_view keep_id) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        return static_cast<std::int64_t>(
            txn.exec_params("UPDATE sessions SET revoked_at = now() "
                            "WHERE user_id = $1 AND id <> $2 AND revoked_at IS NULL",
                            user_id,
                            std::string(keep_id))
                .affected_rows());
    });
}

std::int64_t PgSessionRepository::delete_expired() {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        return static_cast<std::int64_t>(
            txn.exec("DELETE FROM sessions WHERE expires_at < now() OR revoked_at IS NOT NULL")
                .affected_rows());
    });
}

}  // namespace rtc::repositories
