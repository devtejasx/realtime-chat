#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/user_admin_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IUserAdminRepository, reading the columns added by
// migration 0011.
class PgUserAdminRepository final : public database::BaseRepository, public IUserAdminRepository {
  public:
    explicit PgUserAdminRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] std::optional<security::Role> find_role(std::int64_t user_id) override;
    [[nodiscard]] std::optional<bool> is_banned(std::int64_t user_id) override;
    [[nodiscard]] std::optional<AdminUserRecord> find(std::int64_t user_id) override;

    [[nodiscard]] std::vector<AdminUserRecord> list(const AdminUserFilter& filter,
                                                    const dto::Pagination& page) override;
    [[nodiscard]] std::int64_t count(const AdminUserFilter& filter) override;

    [[nodiscard]] security::Role set_role(std::int64_t user_id, security::Role role) override;
    void set_banned(std::int64_t user_id,
                    bool banned,
                    std::optional<std::string> reason,
                    std::optional<std::int64_t> actor_id) override;

    [[nodiscard]] std::vector<std::pair<security::Role, std::int64_t>> counts_by_role() override;
};

}  // namespace rtc::repositories
