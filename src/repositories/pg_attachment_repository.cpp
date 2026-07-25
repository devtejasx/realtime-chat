#include "rtc/repositories/pg_attachment_repository.hpp"

#include <chrono>
#include <string>

#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>

namespace rtc::repositories {
namespace {

using Clock = std::chrono::system_clock;

constexpr const char* kColumns =
    "id, owner_id, message_id, storage_backend, storage_key, thumbnail_key, "
    "original_filename, content_type, kind, byte_size, width, height, checksum, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch";

[[nodiscard]] std::optional<std::string> opt_string(const pqxx::field& f) {
    return f.is_null() ? std::nullopt : std::optional<std::string>(f.as<std::string>());
}
[[nodiscard]] std::optional<int> opt_int(const pqxx::field& f) {
    return f.is_null() ? std::nullopt : std::optional<int>(f.as<int>());
}
[[nodiscard]] std::optional<std::int64_t> opt_int64(const pqxx::field& f) {
    return f.is_null() ? std::nullopt : std::optional<std::int64_t>(f.as<std::int64_t>());
}

[[nodiscard]] models::Attachment map_row(const pqxx::row& row) {
    models::Attachment a;
    a.id = row["id"].as<std::int64_t>();
    a.owner_id = row["owner_id"].as<std::int64_t>();
    a.message_id = opt_int64(row["message_id"]);
    a.storage_backend = row["storage_backend"].as<std::string>();
    a.storage_key = row["storage_key"].as<std::string>();
    a.thumbnail_key = opt_string(row["thumbnail_key"]);
    a.original_filename = row["original_filename"].as<std::string>();
    a.content_type = row["content_type"].as<std::string>();
    a.kind = models::attachment_kind_from_string(row["kind"].as<std::string>())
                 .value_or(models::AttachmentKind::kOther);
    a.byte_size = row["byte_size"].as<std::int64_t>();
    a.width = opt_int(row["width"]);
    a.height = opt_int(row["height"]);
    a.checksum = opt_string(row["checksum"]);
    a.created_at = Clock::from_time_t(row["created_epoch"].as<std::time_t>());
    return a;
}

}  // namespace

models::Attachment PgAttachmentRepository::create(const NewAttachment& input) {
    return with_transaction([&](pqxx::work& txn) -> models::Attachment {
        const std::string sql =
            std::string("INSERT INTO attachments (owner_id, storage_backend, storage_key, "
                        "original_filename, content_type, kind, byte_size, checksum) "
                        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8) RETURNING ") +
            kColumns;
        const auto result = txn.exec_params(
            sql, input.owner_id, input.storage_backend, input.storage_key,
            input.original_filename, input.content_type,
            std::string(models::to_string(input.kind)), input.byte_size, input.checksum);
        return map_row(result.front());
    });
}

std::optional<models::Attachment> PgAttachmentRepository::find_by_id(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::Attachment> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns + " FROM attachments WHERE id = $1", id);
        if (result.empty()) return std::nullopt;
        return map_row(result.front());
    });
}

std::vector<models::Attachment> PgAttachmentRepository::list_for_message(
    std::int64_t message_id) {
    return with_transaction([&](pqxx::work& txn) -> std::vector<models::Attachment> {
        const auto result = txn.exec_params(
            std::string("SELECT ") + kColumns +
                " FROM attachments WHERE message_id = $1 ORDER BY id ASC",
            message_id);
        std::vector<models::Attachment> out;
        out.reserve(result.size());
        for (const auto& row : result) {
            out.push_back(map_row(row));
        }
        return out;
    });
}

std::size_t PgAttachmentRepository::link_to_message(
    const std::vector<std::int64_t>& attachment_ids, std::int64_t message_id,
    std::int64_t owner_id) {
    if (attachment_ids.empty()) {
        return 0;
    }
    return with_transaction([&](pqxx::work& txn) -> std::size_t {
        std::size_t linked = 0;
        for (const std::int64_t id : attachment_ids) {
            // Only the owner's still-unattached attachments may be linked.
            const auto result = txn.exec_params(
                "UPDATE attachments SET message_id = $2 "
                "WHERE id = $1 AND owner_id = $3 AND message_id IS NULL",
                id, message_id, owner_id);
            linked += result.affected_rows();
        }
        return linked;
    });
}

void PgAttachmentRepository::update_media_meta(std::int64_t id, std::optional<int> width,
                                               std::optional<int> height,
                                               std::optional<std::string> thumbnail_key) {
    with_transaction([&](pqxx::work& txn) {
        txn.exec_params(
            "UPDATE attachments SET "
            "width = COALESCE($2, width), "
            "height = COALESCE($3, height), "
            "thumbnail_key = COALESCE($4, thumbnail_key) "
            "WHERE id = $1",
            id, width, height, thumbnail_key);
    });
}

bool PgAttachmentRepository::remove(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        return txn.exec_params("DELETE FROM attachments WHERE id = $1", id).affected_rows() > 0;
    });
}

}  // namespace rtc::repositories
