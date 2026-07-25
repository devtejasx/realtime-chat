#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/reaction_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IReactionRepository.
class PgReactionRepository final : public database::BaseRepository, public IReactionRepository {
public:
    explicit PgReactionRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Reaction upsert(std::int64_t message_id, std::int64_t user_id,
                                          std::string_view emoji) override;
    bool remove(std::int64_t message_id, std::int64_t user_id) override;
    [[nodiscard]] std::vector<models::Reaction> list_for_message(std::int64_t message_id) override;
};

}  // namespace rtc::repositories
