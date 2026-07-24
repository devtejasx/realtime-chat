#include "rtc/repositories/pg_message_repository.hpp"

#include <chrono>
#include <string>

#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>

#include "rtc/errors/exceptions.hpp"

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, conversation_id, sender_id, type, content, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM updated_at)::bigint AS updated_epoch, "
    "EXTRACT(EPOCH FROM edited_at)::bigint AS edited_epoch, "
    "EXTRACT(EPOCH FROM deleted_at)::bigint AS deleted_epoch";

[[nodiscard]] std::optional<utils::TimePoint> read_opt_time(const pqxx::field& field) {
    if (field.is_null()) return std::nullopt;
    return Clock::from_time_t(field.as<std::time_t>());
}

[[nodiscard]] models::Message map_message(const pqxx::row& row) {
    models::Message m;
    m.id = row["id"].as<std::int64_t>();
    m.conversation_id = row["conversation_id"].as<std::int64_t>();
    m.sender_id = row["sender_id"].as<std::int64_t>();
    m.type = models::message_type_from_string(row["type"].as<std::string>())
                 .value_or(models::MessageType::kText);
    m.content = row["content"].as<std::string>();
    m.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    m.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    m.edited_at = read_opt_time(row["edited_epoch"]);
    m.deleted_at = read_opt_time(row["deleted_epoch"]);
    return m;
}

}  // namespace

models::Message PgMessageRepository::create(const NewMessage& input) {
    return with_transaction([&](pqxx::work& txn) -> models::Message {
        // Persist the message and advance the conversation's activity timestamp
        // in the same transaction — the message and its side effect commit atomically.
        const std::string sql =
            std::string("INSERT INTO messages (conversation_id, sender_id, type, content) "
                        "VALUES ($1, $2, $3, $4) RETURNING ") +
            kColumns;
        const auto message = map_message(
            txn.exec_params(sql, input.conversation_id, input.sender_id,
                            std::string(models::to_string(input.type)), input.content)
                .front());

        txn.exec_params("UPDATE conversations SET last_message_at = now() WHERE id = $1",
                        input.conversation_id);
        return message;
    });
}

std::optional<models::Message> PgMessageRepository::find_by_id(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::Message> {
        const auto result =
            txn.exec_params(std::string("SELECT ") + kColumns + " FROM messages WHERE id = $1", id);
        if (result.empty()) return std::nullopt;
        return map_message(result.front());
    });
}

std::vector<models::Message> PgMessageRepository::list(const MessageFilter& filter,
                                                       const dto::Pagination& page) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Message> {
        // A single fixed-shape statement expresses all optional filters via
        // "$n IS NULL OR <predicate>", so PostgreSQL can plan/prepare it once.
        // Keyword search uses the GIN full-text index and skips deleted rows.
        const std::string sql =
            std::string("SELECT ") + kColumns +
            " FROM messages "
            "WHERE conversation_id = $1 "
            "AND ($2::bigint IS NULL OR sender_id = $2) "
            "AND ($3::text IS NULL OR (to_tsvector('simple', content) @@ "
            "     plainto_tsquery('simple', $3) AND deleted_at IS NULL)) "
            "AND ($4::bigint IS NULL OR id < $4) "
            "AND ($5::bigint IS NULL OR id > $5) "
            "ORDER BY id DESC LIMIT $6 OFFSET $7";
        const auto result =
            txn.exec_params(sql, filter.conversation_id, filter.sender_id, filter.keyword,
                            page.before_id, page.after_id, page.limit, page.offset);
        std::vector<models::Message> out;
        out.reserve(result.size());
        for (const auto& row : result) {
            out.push_back(map_message(row));
        }
        return out;
    });
}

models::Message PgMessageRepository::update_content(std::int64_t id, std::string_view content) {
    return with_transaction([&](pqxx::work& txn) -> models::Message {
        const std::string sql =
            std::string("UPDATE messages SET content = $2, edited_at = now() "
                        "WHERE id = $1 AND deleted_at IS NULL RETURNING ") +
            kColumns;
        const auto result = txn.exec_params(sql, id, std::string(content));
        if (result.empty()) {
            throw rtc::errors::NotFoundException("Message not found or already deleted");
        }
        return map_message(result.front());
    });
}

models::Message PgMessageRepository::soft_delete(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> models::Message {
        const std::string sql =
            std::string("UPDATE messages SET deleted_at = now() "
                        "WHERE id = $1 AND deleted_at IS NULL RETURNING ") +
            kColumns;
        const auto result = txn.exec_params(sql, id);
        if (result.empty()) {
            throw rtc::errors::NotFoundException("Message not found or already deleted");
        }
        return map_message(result.front());
    });
}

}  // namespace rtc::repositories
