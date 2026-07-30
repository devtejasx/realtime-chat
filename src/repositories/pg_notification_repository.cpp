#include "rtc/repositories/pg_notification_repository.hpp"

#include <chrono>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, user_id, type, payload::text AS payload_text, "
    "EXTRACT(EPOCH FROM read_at)::bigint AS read_epoch, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch";

[[nodiscard]] models::Notification map_row(const pqxx::row& row) {
    models::Notification n;
    n.id = row["id"].as<std::int64_t>();
    n.user_id = row["user_id"].as<std::int64_t>();
    // Unknown types fall back to new_message so a forward-compatible producer
    // never breaks an older reader.
    const std::string type = row["type"].as<std::string>();
    n.type = models::NotificationType::kNewMessage;
    for (auto candidate : {models::NotificationType::kNewMessage,
                           models::NotificationType::kMention,
                           models::NotificationType::kAddedToGroup,
                           models::NotificationType::kRemovedFromGroup,
                           models::NotificationType::kReactionAdded,
                           models::NotificationType::kProfileUpdated}) {
        if (models::to_string(candidate) == type) {
            n.type = candidate;
            break;
        }
    }
    n.payload = nlohmann::json::parse(row["payload_text"].as<std::string>(), nullptr, false);
    if (n.payload.is_discarded()) {
        n.payload = nlohmann::json::object();
    }
    if (!row["read_epoch"].is_null()) {
        n.read_at = Clock::from_time_t(row["read_epoch"].as<std::time_t>());
    }
    n.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    return n;
}

}  // namespace

models::Notification PgNotificationRepository::create(std::int64_t user_id,
                                                      models::NotificationType type,
                                                      const nlohmann::json& payload) {
    return with_transaction([&](pqxx::work& txn) -> models::Notification {
        const std::string sql = std::string(
                                    "INSERT INTO notifications (user_id, type, payload) "
                                    "VALUES ($1, $2, $3::jsonb) RETURNING ") +
                                kColumns;
        return map_row(
            txn.exec_params(sql, user_id, std::string(models::to_string(type)), payload.dump())
                .front());
    });
}

std::vector<models::Notification> PgNotificationRepository::list_for_user(
    std::int64_t user_id, const dto::Pagination& page, bool unread_only) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Notification> {
        const std::string sql = std::string("SELECT ") + kColumns +
                                " FROM notifications WHERE user_id = $1 "
                                "AND ($4 = false OR read_at IS NULL) "
                                "ORDER BY id DESC LIMIT $2 OFFSET $3";
        const auto result = txn.exec_params(sql, user_id, page.limit, page.offset, unread_only);
        std::vector<models::Notification> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.push_back(map_row(row));
        }
        return out;
    });
}

std::int64_t PgNotificationRepository::unread_count(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        const auto row = txn.exec_params1(
            "SELECT COUNT(*) FROM notifications WHERE user_id = $1 AND read_at IS NULL", user_id);
        return row[0].as<std::int64_t>();
    });
}

bool PgNotificationRepository::mark_read(std::int64_t id, std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        return txn.exec_params(
                      "UPDATE notifications SET read_at = now() "
                      "WHERE id = $1 AND user_id = $2 AND read_at IS NULL",
                      id,
                      user_id)
                   .affected_rows() > 0;
    });
}

std::int64_t PgNotificationRepository::mark_all_read(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::int64_t {
        return static_cast<std::int64_t>(txn.exec_params("UPDATE notifications SET read_at = now() "
                                                         "WHERE user_id = $1 AND read_at IS NULL",
                                                         user_id)
                                             .affected_rows());
    });
}

bool PgNotificationRepository::remove(std::int64_t id, std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        return txn.exec_params(
                      "DELETE FROM notifications WHERE id = $1 AND user_id = $2", id, user_id)
                   .affected_rows() > 0;
    });
}

}  // namespace rtc::repositories
