#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/audit_log_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IAuditLogRepository.
class PgAuditLogRepository final : public database::BaseRepository, public IAuditLogRepository {
public:
    explicit PgAuditLogRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    bool append(const NewAuditLog& input) override;

    [[nodiscard]] std::vector<models::AuditLog> search(const AuditLogFilter& filter,
                                                      const dto::Pagination& page) override;
    [[nodiscard]] std::int64_t count(const AuditLogFilter& filter) override;
    [[nodiscard]] std::optional<models::AuditLog> find_by_id(std::int64_t id) override;
    [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>> counts_by_type(
        const AuditLogFilter& filter) override;
};

}  // namespace rtc::repositories
