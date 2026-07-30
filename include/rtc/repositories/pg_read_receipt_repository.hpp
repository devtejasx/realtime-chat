#pragma once

#include <cstdint>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/read_receipt_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IReadReceiptRepository.
class PgReadReceiptRepository final : public database::BaseRepository,
                                      public IReadReceiptRepository {
  public:
    explicit PgReadReceiptRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::ReadReceipt upsert_state(std::int64_t message_id,
                                                   std::int64_t user_id,
                                                   models::ReceiptState state) override;
    void mark_conversation_read(std::int64_t conversation_id,
                                std::int64_t user_id,
                                std::int64_t up_to_message_id) override;
    [[nodiscard]] std::vector<models::ReadReceipt> list_for_message(
        std::int64_t message_id) override;
};

}  // namespace rtc::repositories
