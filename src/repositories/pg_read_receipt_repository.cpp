#include "rtc/repositories/pg_read_receipt_repository.hpp"

#include <chrono>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, message_id, user_id, state, EXTRACT(EPOCH FROM updated_at)::bigint AS updated_epoch";

// SQL expression yielding the monotonic rank of a state column/expression.
[[nodiscard]] std::string rank_sql(std::string_view expr) {
    return std::string("CASE ") + std::string(expr) +
           " WHEN 'sent' THEN 0 WHEN 'delivered' THEN 1 WHEN 'read' THEN 2 ELSE 0 END";
}

[[nodiscard]] models::ReadReceipt map_receipt(const pqxx::row& row) {
    models::ReadReceipt r;
    r.id = row["id"].as<std::int64_t>();
    r.message_id = row["message_id"].as<std::int64_t>();
    r.user_id = row["user_id"].as<std::int64_t>();
    r.state = models::receipt_state_from_string(row["state"].as<std::string>())
                  .value_or(models::ReceiptState::kSent);
    r.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    return r;
}

}  // namespace

models::ReadReceipt PgReadReceiptRepository::upsert_state(std::int64_t message_id,
                                                          std::int64_t user_id,
                                                          models::ReceiptState state) {
    return with_transaction([&](pqxx::work& txn) -> models::ReadReceipt {
        // On conflict, only advance when the incoming rank exceeds the stored
        // rank, so a late "delivered" can never overwrite a "read".
        const std::string sql =
            std::string(
                "INSERT INTO read_receipts (message_id, user_id, state) "
                "VALUES ($1, $2, $3) "
                "ON CONFLICT (message_id, user_id) DO UPDATE SET state = CASE WHEN ") +
            rank_sql("EXCLUDED.state") + " > " + rank_sql("read_receipts.state") +
            " THEN EXCLUDED.state ELSE read_receipts.state END, updated_at = now() RETURNING " +
            kColumns;
        const auto result =
            txn.exec_params(sql, message_id, user_id, std::string(models::to_string(state)));
        return map_receipt(result.front());
    });
}

void PgReadReceiptRepository::mark_conversation_read(std::int64_t conversation_id,
                                                     std::int64_t user_id,
                                                     std::int64_t up_to_message_id) {
    with_transaction([&](pqxx::work& txn) {
        // One statement marks the backlog read: insert receipts for the user's
        // unread messages (excluding their own) and advance any existing ones.
        txn.exec_params(
            "INSERT INTO read_receipts (message_id, user_id, state) "
            "SELECT m.id, $2, 'read' FROM messages m "
            "WHERE m.conversation_id = $1 AND m.id <= $3 AND m.sender_id <> $2 "
            "ON CONFLICT (message_id, user_id) "
            "DO UPDATE SET state = 'read', updated_at = now() "
            "WHERE read_receipts.state <> 'read'",
            conversation_id,
            user_id,
            up_to_message_id);
    });
}

std::vector<models::ReadReceipt> PgReadReceiptRepository::list_for_message(
    std::int64_t message_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::ReadReceipt> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns + " FROM read_receipts WHERE message_id = $1",
            message_id);
        std::vector<models::ReadReceipt> out;
        out.reserve(static_cast<std::size_t>(result.size()));
        for (const auto& row : result) {
            out.push_back(map_receipt(row));
        }
        return out;
    });
}

}  // namespace rtc::repositories
