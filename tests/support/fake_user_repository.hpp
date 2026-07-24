#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/errors/exceptions.hpp"
#include "rtc/models/user.hpp"
#include "rtc/repositories/user_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory IUserRepository for unit-testing services and controllers without
// a database. Mirrors the real repository's contract: case-insensitive
// uniqueness enforced with a ConflictException, and identifier lookup across
// username and email.
class FakeUserRepository final : public repositories::IUserRepository {
public:
    [[nodiscard]] models::User create(const repositories::NewUser& input) override {
        if (find_by_username(input.username) || find_by_email(input.email)) {
            const bool email_conflict = find_by_email(input.email).has_value();
            throw rtc::errors::ConflictException(
                email_conflict ? "Email is already registered" : "Username is already taken");
        }
        models::User user;
        user.id = next_id_++;
        user.username = input.username;
        user.email = input.email;
        user.password_hash = input.password_hash;
        user.created_at = utils::now();
        user.updated_at = user.created_at;
        users_.push_back(user);
        return user;
    }

    [[nodiscard]] std::optional<models::User> find_by_id(std::int64_t id) override {
        for (const auto& user : users_) {
            if (user.id == id) return user;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<models::User> find_by_username(
        std::string_view username) override {
        for (const auto& user : users_) {
            if (iequals(user.username, username)) return user;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<models::User> find_by_email(std::string_view email) override {
        for (const auto& user : users_) {
            if (iequals(user.email, email)) return user;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<models::User> find_by_identifier(
        std::string_view identifier) override {
        if (auto by_name = find_by_username(identifier)) return by_name;
        return find_by_email(identifier);
    }

    [[nodiscard]] bool exists_by_username(std::string_view username) override {
        return find_by_username(username).has_value();
    }

    [[nodiscard]] bool exists_by_email(std::string_view email) override {
        return find_by_email(email).has_value();
    }

    [[nodiscard]] models::User update_profile(std::int64_t id,
                                              const repositories::ProfileUpdate& update) override {
        for (auto& user : users_) {
            if (user.id == id) {
                if (update.display_name_set) user.display_name = update.display_name;
                if (update.bio_set) user.bio = update.bio;
                if (update.avatar_url_set) user.avatar_url = update.avatar_url;
                user.updated_at = utils::now();
                return user;
            }
        }
        throw rtc::errors::NotFoundException("User not found");
    }

    [[nodiscard]] std::size_t count() const noexcept { return users_.size(); }

private:
    static bool iequals(std::string_view a, std::string_view b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) ==
                   std::tolower(static_cast<unsigned char>(y));
        });
    }

    std::vector<models::User> users_;
    std::int64_t next_id_ = 1;
};

}  // namespace rtc::testing
