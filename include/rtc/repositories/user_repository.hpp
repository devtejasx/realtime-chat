#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/models/user.hpp"

namespace rtc::repositories {

// Parameters for creating a new user row. The password is already hashed by
// the service layer before it reaches the repository — repositories never see
// plaintext secrets.
struct NewUser {
    std::string username;
    std::string email;
    std::string password_hash;
};

// A partial profile update (Phase 2). Each field carries a "set" flag so the
// repository can build an UPDATE touching only the supplied columns; a set
// field with a nullopt value clears the column (SET column = NULL).
struct ProfileUpdate {
    bool display_name_set = false;
    std::optional<std::string> display_name;
    bool bio_set = false;
    std::optional<std::string> bio;
    bool avatar_url_set = false;
    std::optional<std::string> avatar_url;
};

// Persistence boundary for User aggregates. Depends only on the domain model,
// so services can be unit-tested against an in-memory fake implementation.
class IUserRepository {
public:
    virtual ~IUserRepository() = default;

    // Inserts a new user and returns it with id and timestamps populated.
    // Throws rtc::errors::ConflictException if the username or email already
    // exists, and DatabaseException on any other persistence failure.
    [[nodiscard]] virtual models::User create(const NewUser& input) = 0;

    [[nodiscard]] virtual std::optional<models::User> find_by_id(std::int64_t id) = 0;
    [[nodiscard]] virtual std::optional<models::User> find_by_username(
        std::string_view username) = 0;
    [[nodiscard]] virtual std::optional<models::User> find_by_email(
        std::string_view email) = 0;

    // Finds a user by either username or email (case-insensitive); used by login.
    [[nodiscard]] virtual std::optional<models::User> find_by_identifier(
        std::string_view identifier) = 0;

    [[nodiscard]] virtual bool exists_by_username(std::string_view username) = 0;
    [[nodiscard]] virtual bool exists_by_email(std::string_view email) = 0;

    // Applies a partial profile update and returns the refreshed user. Throws
    // NotFoundException if the user no longer exists. A no-op update (no fields
    // set) returns the current row unchanged.
    [[nodiscard]] virtual models::User update_profile(std::int64_t id,
                                                      const ProfileUpdate& update) = 0;
};

}  // namespace rtc::repositories
