#include "rtc/repositories/pg_user_repository.hpp"

#include <chrono>
#include <optional>
#include <pqxx/except>
#include <pqxx/result>
#include <pqxx/row>
#include <pqxx/transaction>
#include <string>

#include "rtc/errors/exceptions.hpp"

namespace rtc::repositories {
namespace {

// Shared projection. Timestamps are returned as Unix epoch seconds to sidestep
// any client/server timezone-formatting ambiguity when mapping back to a
// time_point.
constexpr const char* kSelectColumns =
    "id, username, email, password_hash, display_name, bio, avatar_url, "
    "EXTRACT(EPOCH FROM created_at)::bigint AS created_epoch, "
    "EXTRACT(EPOCH FROM updated_at)::bigint AS updated_epoch";

// Reads a nullable text column into std::optional<std::string>.
[[nodiscard]] std::optional<std::string> read_optional(const pqxx::field& field) {
    if (field.is_null()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

[[nodiscard]] models::User map_row(const pqxx::row& row) {
    models::User user;
    user.id = row["id"].as<std::int64_t>();
    user.username = row["username"].as<std::string>();
    user.email = row["email"].as<std::string>();
    user.password_hash = row["password_hash"].as<std::string>();
    user.display_name = read_optional(row["display_name"]);
    user.bio = read_optional(row["bio"]);
    user.avatar_url = read_optional(row["avatar_url"]);
    user.created_at =
        std::chrono::system_clock::from_time_t(row["created_epoch"].as<std::time_t>());
    user.updated_at =
        std::chrono::system_clock::from_time_t(row["updated_epoch"].as<std::time_t>());
    return user;
}

[[nodiscard]] std::optional<models::User> map_optional(const pqxx::result& result) {
    if (result.empty()) {
        return std::nullopt;
    }
    return map_row(result.front());
}

}  // namespace

models::User PgUserRepository::create(const NewUser& input) {
    return with_transaction([&](pqxx::work& txn) -> models::User {
        try {
            const std::string sql = std::string(
                                        "INSERT INTO users (username, email, password_hash) "
                                        "VALUES ($1, $2, $3) RETURNING ") +
                                    kSelectColumns;
            const pqxx::result result =
                txn.exec_params(sql, input.username, input.email, input.password_hash);
            return map_row(result.front());
        } catch (const pqxx::unique_violation& ex) {
            // Translate the DB-level uniqueness guarantee into a domain conflict,
            // pinpointing the offending field from the violated index name.
            const std::string what = ex.what();
            if (what.find("email") != std::string::npos) {
                throw rtc::errors::ConflictException("Email is already registered", "field=email");
            }
            throw rtc::errors::ConflictException("Username is already taken", "field=username");
        }
    });
}

std::optional<models::User> PgUserRepository::find_by_id(std::int64_t id) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::User> {
        const std::string sql =
            std::string("SELECT ") + kSelectColumns + " FROM users WHERE id = $1";
        return map_optional(txn.exec_params(sql, id));
    });
}

std::optional<models::User> PgUserRepository::find_by_username(std::string_view username) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::User> {
        const std::string sql = std::string("SELECT ") + kSelectColumns +
                                " FROM users WHERE LOWER(username) = LOWER($1)";
        return map_optional(txn.exec_params(sql, std::string(username)));
    });
}

std::optional<models::User> PgUserRepository::find_by_email(std::string_view email) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::User> {
        const std::string sql =
            std::string("SELECT ") + kSelectColumns + " FROM users WHERE LOWER(email) = LOWER($1)";
        return map_optional(txn.exec_params(sql, std::string(email)));
    });
}

std::optional<models::User> PgUserRepository::find_by_identifier(std::string_view identifier) {
    return with_transaction([&](pqxx::work& txn) -> std::optional<models::User> {
        const std::string sql =
            std::string("SELECT ") + kSelectColumns +
            " FROM users WHERE LOWER(username) = LOWER($1) OR LOWER(email) = LOWER($1) LIMIT 1";
        return map_optional(txn.exec_params(sql, std::string(identifier)));
    });
}

bool PgUserRepository::exists_by_username(std::string_view username) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        const auto row =
            txn.exec_params1("SELECT EXISTS(SELECT 1 FROM users WHERE LOWER(username) = LOWER($1))",
                             std::string(username));
        return row[0].as<bool>();
    });
}

bool PgUserRepository::exists_by_email(std::string_view email) {
    return with_transaction([&](pqxx::work& txn) -> bool {
        const auto row =
            txn.exec_params1("SELECT EXISTS(SELECT 1 FROM users WHERE LOWER(email) = LOWER($1))",
                             std::string(email));
        return row[0].as<bool>();
    });
}

models::User PgUserRepository::update_profile(std::int64_t id, const ProfileUpdate& update) {
    return with_transaction([&](pqxx::work& txn) -> models::User {
        // No-op update: return the current row without bumping updated_at.
        if (!update.display_name_set && !update.bio_set && !update.avatar_url_set) {
            const std::string sql =
                std::string("SELECT ") + kSelectColumns + " FROM users WHERE id = $1";
            const auto result = txn.exec_params(sql, id);
            if (result.empty()) {
                throw rtc::errors::NotFoundException("User not found");
            }
            return map_row(result.front());
        }

        // A per-column CASE lets one prepared statement express three
        // independent intents: leave unchanged (flag false), set a value, or
        // clear to NULL (flag true, value null).
        const std::string sql = std::string(
                                    "UPDATE users SET "
                                    "display_name = CASE WHEN $2 THEN $3 ELSE display_name END, "
                                    "bio          = CASE WHEN $4 THEN $5 ELSE bio END, "
                                    "avatar_url   = CASE WHEN $6 THEN $7 ELSE avatar_url END "
                                    "WHERE id = $1 RETURNING ") +
                                kSelectColumns;

        const auto result = txn.exec_params(sql,
                                            id,
                                            update.display_name_set,
                                            update.display_name,
                                            update.bio_set,
                                            update.bio,
                                            update.avatar_url_set,
                                            update.avatar_url);
        if (result.empty()) {
            throw rtc::errors::NotFoundException("User not found");
        }
        return map_row(result.front());
    });
}

}  // namespace rtc::repositories
