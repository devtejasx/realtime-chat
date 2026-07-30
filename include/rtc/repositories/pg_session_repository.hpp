#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/session_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed ISessionRepository.
class PgSessionRepository final : public database::BaseRepository, public ISessionRepository {
  public:
    explicit PgSessionRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Session create(const NewSession& input) override;
    [[nodiscard]] std::optional<models::Session> find_by_id(std::string_view id) override;
    [[nodiscard]] std::vector<models::Session> list_active_for_user(std::int64_t user_id) override;
    void rotate(std::string_view id, std::string_view new_hash) override;
    bool revoke(std::string_view id, std::int64_t user_id) override;
    std::int64_t revoke_all(std::int64_t user_id) override;
    std::int64_t revoke_all_except(std::int64_t user_id, std::string_view keep_id) override;
    std::int64_t delete_expired() override;
};

}  // namespace rtc::repositories
