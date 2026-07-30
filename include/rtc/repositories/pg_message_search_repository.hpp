#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/message_search_repository.hpp"

namespace rtc::repositories {

// PostgreSQL full-text implementation of IMessageSearchRepository, using the
// stored tsvector and trigram indexes added by migration 0013.
class PgMessageSearchRepository final : public database::BaseRepository,
                                        public IMessageSearchRepository {
  public:
    explicit PgMessageSearchRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] std::vector<MessageSearchHit> search(std::int64_t actor_id,
                                                       const MessageSearchQuery& query,
                                                       const dto::Pagination& page) override;
    [[nodiscard]] std::int64_t count(std::int64_t actor_id,
                                     const MessageSearchQuery& query) override;
    [[nodiscard]] bool fuzzy_available() override;

  private:
    // pg_trgm availability is a property of the database that cannot change while
    // the process runs, so it is probed at most once. The mutex guards the probe
    // itself; the result is read through an atomic afterwards.
    std::once_flag trgm_probe_;
    std::atomic<bool> trgm_available_{false};
};

}  // namespace rtc::repositories
