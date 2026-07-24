#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/user_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IUserRepository. Contains only SQL and row mapping; all
// connection/transaction handling is inherited from BaseRepository.
class PgUserRepository final : public database::BaseRepository, public IUserRepository {
public:
    explicit PgUserRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::User create(const NewUser& input) override;

    [[nodiscard]] std::optional<models::User> find_by_id(std::int64_t id) override;
    [[nodiscard]] std::optional<models::User> find_by_username(
        std::string_view username) override;
    [[nodiscard]] std::optional<models::User> find_by_email(std::string_view email) override;
    [[nodiscard]] std::optional<models::User> find_by_identifier(
        std::string_view identifier) override;

    [[nodiscard]] bool exists_by_username(std::string_view username) override;
    [[nodiscard]] bool exists_by_email(std::string_view email) override;
};

}  // namespace rtc::repositories
