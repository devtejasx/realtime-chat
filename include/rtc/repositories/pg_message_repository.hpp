#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/message_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IMessageRepository.
class PgMessageRepository final : public database::BaseRepository, public IMessageRepository {
  public:
    explicit PgMessageRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Message create(const NewMessage& input) override;
    [[nodiscard]] std::optional<models::Message> find_by_id(std::int64_t id) override;
    [[nodiscard]] std::vector<models::Message> list(const MessageFilter& filter,
                                                    const dto::Pagination& page) override;
    [[nodiscard]] models::Message update_content(std::int64_t id,
                                                 std::string_view content) override;
    [[nodiscard]] models::Message soft_delete(std::int64_t id) override;
};

}  // namespace rtc::repositories
