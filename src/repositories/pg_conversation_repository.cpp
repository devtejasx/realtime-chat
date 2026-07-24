#include "rtc/repositories/pg_conversation_repository.hpp"

#include <algorithm>
#include <chrono>
#include <string>

#include <pqxx/except>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>

#include "rtc/errors/exceptions.hpp"

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

[[nodiscard]] std::optional<std::int64_t> read_opt_int64(const pqxx::field& field) {
    return field.is_null() ? std::nullopt : std::optional<std::int64_t>(field.as<std::int64_t>());
}
[[nodiscard]] std::optional<std::string> read_opt_string(const pqxx::field& field) {
    return field.is_null() ? std::nullopt : std::optional<std::string>(field.as<std::string>());
}
[[nodiscard]] std::optional<utils::TimePoint> read_opt_time(const pqxx::field& field) {
    if (field.is_null()) return std::nullopt;
    return Clock::from_time_t(field.as<std::time_t>());
}

// Column projection for the conversations table, optionally aliased for joins.
[[nodiscard]] std::string conv_columns(std::string_view a = "") {
    const std::string p = a.empty() ? std::string{} : std::string(a) + ".";
    return p + "id, " + p + "type, " + p + "name, " + p + "owner_id, " + p + "direct_key, " +
           "EXTRACT(EPOCH FROM " + p + "created_at)::bigint AS created_epoch, " +
           "EXTRACT(EPOCH FROM " + p + "updated_at)::bigint AS updated_epoch, " +
           "EXTRACT(EPOCH FROM " + p + "last_message_at)::bigint AS last_message_epoch";
}

[[nodiscard]] models::Conversation map_conversation(const pqxx::row& row) {
    models::Conversation c;
    c.id = row["id"].as<std::int64_t>();
    c.type = models::conversation_type_from_string(row["type"].as<std::string>())
                 .value_or(models::ConversationType::kDirect);
    c.name = read_opt_string(row["name"]);
    c.owner_id = read_opt_int64(row["owner_id"]);
    c.direct_key = read_opt_string(row["direct_key"]);
    c.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    c.updated_at = Clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    c.last_message_at = read_opt_time(row["last_message_epoch"]);
    return c;
}

[[nodiscard]] models::ConversationParticipant map_participant(const pqxx::row& row) {
    models::ConversationParticipant p;
    p.id = row["id"].as<std::int64_t>();
    p.conversation_id = row["conversation_id"].as<std::int64_t>();
    p.user_id = row["user_id"].as<std::int64_t>();
    p.role = models::participant_role_from_string(row["role"].as<std::string>())
                 .value_or(models::ParticipantRole::kMember);
    p.joined_at = Clock::from_time_t(row["joined_epoch"].as<std::time_t>());
    p.last_read_message_id = read_opt_int64(row["last_read_message_id"]);
    return p;
}

constexpr const char* kParticipantColumns =
    "id, conversation_id, user_id, role, "
    "EXTRACT(EPOCH FROM joined_at)::bigint AS joined_epoch, last_read_message_id";

[[nodiscard]] std::string direct_key(std::int64_t a, std::int64_t b) {
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    return std::to_string(lo) + ":" + std::to_string(hi);
}

}  // namespace

models::Conversation PgConversationRepository::create_or_get_direct(std::int64_t user_a,
                                                                    std::int64_t user_b) {
    return with_transaction([&](pqxx::work& txn) -> models::Conversation {
        const std::string key = direct_key(user_a, user_b);
        // Upsert on the partial unique index makes this race-safe and always
        // returns exactly one row (existing or freshly inserted).
        const std::string sql =
            "INSERT INTO conversations (type, direct_key) VALUES ('direct', $1) "
            "ON CONFLICT (direct_key) WHERE direct_key IS NOT NULL "
            "DO UPDATE SET direct_key = EXCLUDED.direct_key RETURNING " +
            conv_columns();
        const auto conv = map_conversation(txn.exec_params(sql, key).front());

        txn.exec_params(
            "INSERT INTO conversation_participants (conversation_id, user_id, role) "
            "VALUES ($1, $2, 'member'), ($1, $3, 'member') "
            "ON CONFLICT (conversation_id, user_id) DO NOTHING",
            conv.id, user_a, user_b);
        return conv;
    });
}

models::Conversation PgConversationRepository::create_group(
    std::int64_t owner_id, std::string_view name, const std::vector<std::int64_t>& member_ids) {
    return with_transaction([&](pqxx::work& txn) -> models::Conversation {
        const std::string sql =
            "INSERT INTO conversations (type, name, owner_id) VALUES ('group', $1, $2) "
            "RETURNING " +
            conv_columns();
        const auto conv = map_conversation(txn.exec_params(sql, std::string(name), owner_id).front());

        txn.exec_params(
            "INSERT INTO conversation_participants (conversation_id, user_id, role) "
            "VALUES ($1, $2, 'owner')",
            conv.id, owner_id);

        for (const std::int64_t member : member_ids) {
            if (member == owner_id) {
                continue;  // owner already added
            }
            txn.exec_params(
                "INSERT INTO conversation_participants (conversation_id, user_id, role) "
                "VALUES ($1, $2, 'member') ON CONFLICT (conversation_id, user_id) DO NOTHING",
                conv.id, member);
        }
        return conv;
    });
}

std::optional<models::Conversation> PgConversationRepository::find_by_id(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::Conversation> {
        const auto result =
            txn.exec_params("SELECT " + conv_columns() + " FROM conversations WHERE id = $1", id);
        if (result.empty()) return std::nullopt;
        return map_conversation(result.front());
    });
}

std::vector<models::Conversation> PgConversationRepository::list_for_user(
    std::int64_t user_id, const dto::Pagination& page) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Conversation> {
        const std::string sql =
            "SELECT " + conv_columns("c") +
            " FROM conversations c "
            "JOIN conversation_participants p ON p.conversation_id = c.id "
            "WHERE p.user_id = $1 "
            "ORDER BY c.last_message_at DESC NULLS LAST, c.id DESC "
            "LIMIT $2 OFFSET $3";
        const auto result = txn.exec_params(sql, user_id, page.limit, page.offset);
        std::vector<models::Conversation> out;
        out.reserve(result.size());
        for (const auto& row : result) {
            out.push_back(map_conversation(row));
        }
        return out;
    });
}

std::vector<models::ConversationParticipant> PgConversationRepository::list_participants(
    std::int64_t conversation_id) {
    return with_transaction(
        [&](pqxx::work& txn) -> std::vector<models::ConversationParticipant> {
            const std::string sql = std::string("SELECT ") + kParticipantColumns +
                                    " FROM conversation_participants WHERE conversation_id = $1 "
                                    "ORDER BY joined_at ASC, id ASC";
            const auto result = txn.exec_params(sql, conversation_id);
            std::vector<models::ConversationParticipant> out;
            out.reserve(result.size());
            for (const auto& row : result) {
                out.push_back(map_participant(row));
            }
            return out;
        });
}

std::vector<std::int64_t> PgConversationRepository::list_participant_ids(
    std::int64_t conversation_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<std::int64_t> {
        const auto result = txn.exec_params(
            "SELECT user_id FROM conversation_participants WHERE conversation_id = $1",
            conversation_id);
        std::vector<std::int64_t> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row[0].as<std::int64_t>());
        }
        return ids;
    });
}

std::vector<std::int64_t> PgConversationRepository::list_conversation_ids(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<std::int64_t> {
        const auto result = txn.exec_params(
            "SELECT conversation_id FROM conversation_participants WHERE user_id = $1", user_id);
        std::vector<std::int64_t> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row[0].as<std::int64_t>());
        }
        return ids;
    });
}

std::vector<std::int64_t> PgConversationRepository::list_peer_ids(std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<std::int64_t> {
        // Self-join over shared conversations yields the distinct set of users
        // who co-participate with `user_id` (excluding themselves).
        const auto result = txn.exec_params(
            "SELECT DISTINCT p2.user_id FROM conversation_participants p1 "
            "JOIN conversation_participants p2 ON p1.conversation_id = p2.conversation_id "
            "WHERE p1.user_id = $1 AND p2.user_id <> $1",
            user_id);
        std::vector<std::int64_t> ids;
        ids.reserve(result.size());
        for (const auto& row : result) {
            ids.push_back(row[0].as<std::int64_t>());
        }
        return ids;
    });
}

std::optional<models::ConversationParticipant> PgConversationRepository::find_participant(
    std::int64_t conversation_id, std::int64_t user_id) {
    return with_transaction(
        [&](pqxx::work& txn) -> std::optional<models::ConversationParticipant> {
            const std::string sql =
                std::string("SELECT ") + kParticipantColumns +
                " FROM conversation_participants WHERE conversation_id = $1 AND user_id = $2";
            const auto result = txn.exec_params(sql, conversation_id, user_id);
            if (result.empty()) return std::nullopt;
            return map_participant(result.front());
        });
}

bool PgConversationRepository::is_participant(std::int64_t conversation_id, std::int64_t user_id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        const auto row = txn.exec_params1(
            "SELECT EXISTS(SELECT 1 FROM conversation_participants "
            "WHERE conversation_id = $1 AND user_id = $2)",
            conversation_id, user_id);
        return row[0].as<bool>();
    });
}

void PgConversationRepository::add_participant(std::int64_t conversation_id, std::int64_t user_id,
                                               models::ParticipantRole role) {
    with_transaction([&](pqxx::work& txn) {
        try {
            txn.exec_params(
                "INSERT INTO conversation_participants (conversation_id, user_id, role) "
                "VALUES ($1, $2, $3)",
                conversation_id, user_id, std::string(models::to_string(role)));
        } catch (const pqxx::unique_violation&) {
            throw rtc::errors::ConflictException("User is already a member of the conversation");
        }
    });
}

void PgConversationRepository::remove_participant(std::int64_t conversation_id,
                                                  std::int64_t user_id) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params(
            "DELETE FROM conversation_participants WHERE conversation_id = $1 AND user_id = $2",
            conversation_id, user_id);
    });
}

void PgConversationRepository::rename(std::int64_t conversation_id, std::string_view name) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params("UPDATE conversations SET name = $2 WHERE id = $1", conversation_id,
                        std::string(name));
    });
}

void PgConversationRepository::transfer_ownership(std::int64_t conversation_id,
                                                  std::int64_t new_owner_id) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params("UPDATE conversations SET owner_id = $2 WHERE id = $1", conversation_id,
                        new_owner_id);
        // Demote any current owner, then promote the new one — both in one txn.
        txn.exec_params(
            "UPDATE conversation_participants SET role = 'member' "
            "WHERE conversation_id = $1 AND role = 'owner' AND user_id <> $2",
            conversation_id, new_owner_id);
        txn.exec_params(
            "UPDATE conversation_participants SET role = 'owner' "
            "WHERE conversation_id = $1 AND user_id = $2",
            conversation_id, new_owner_id);
    });
}

void PgConversationRepository::remove(std::int64_t conversation_id) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params("DELETE FROM conversations WHERE id = $1", conversation_id);
    });
}

void PgConversationRepository::update_last_read(std::int64_t conversation_id,
                                                std::int64_t user_id, std::int64_t message_id) {
    with_transaction([&](pqxx::work& txn) {
        // GREATEST keeps the marker monotonic even if events arrive out of order.
        txn.exec_params(
            "UPDATE conversation_participants "
            "SET last_read_message_id = GREATEST(COALESCE(last_read_message_id, 0), $3) "
            "WHERE conversation_id = $1 AND user_id = $2",
            conversation_id, user_id, message_id);
    });
}

}  // namespace rtc::repositories
