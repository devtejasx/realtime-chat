#include "rtc/repositories/pg_reaction_repository.hpp"

#include <chrono>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, message_id, user_id, emoji, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM updated_at)::bigint AS updated_epoch";

[[nodiscard]] models::Reaction map_row(const pqxx::row& row) {
    models::Reaction r;
    r.id = row["id"].as<std::int64_t>();
    r.message_id = row["message_id"].as<std::int64_t>();
    r.user_id = row["user_id"].as<std::int64_t>();
    r.emoji = row["emoji"].as<std::string>();
    r.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    r.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    return r;
}

}  // namespace

models::Reaction PgReactionRepository::upsert(std::int64_t message_id,
                                              std::int64_t user_id,
                                              std::string_view emoji) {
    return with_transaction([&](pqxx::work& txn) -> models::Reaction {
        const std::string sql =
            std::string(
                "INSERT INTO message_reactions (message_id, user_id, emoji) "
                "VALUES ($1, $2, $3) "
                "ON CONFLICT (message_id, user_id) "
                "DO UPDATE SET emoji = EXCLUDED.emoji, updated_at = now() RETURNING ") +
            kColumns;
        return map_row(txn.exec_params(sql, message_id, user_id, std::string(emoji)).front());
    });
}

bool PgReactionRepository::remove(std::int64_t message_id, std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        return txn.exec_params(
                      "DELETE FROM message_reactions WHERE message_id = $1 AND user_id = $2",
                      message_id,
                      user_id)
                   .affected_rows() > 0;
    });
}

std::vector<models::Reaction> PgReactionRepository::list_for_message(std::int64_t message_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Reaction> {
        const auto result =
            txn.exec_params(std::string("SELECT ") + kColumns +
                                " FROM message_reactions WHERE message_id = $1 ORDER BY id ASC",
                            message_id);
        std::vector<models::Reaction> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.push_back(map_row(row));
        }
        return out;
    });
}

}  // namespace rtc::repositories
