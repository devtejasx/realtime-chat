#pragma once

#include <cstdint>
#include <vector>

#include "rtc/models/read_receipt.hpp"

namespace rtc::repositories {

// Persistence boundary for read receipts. State advances monotonically; the
// implementation guarantees a receipt never regresses (read -> delivered).
class IReadReceiptRepository {
  public:
    virtual ~IReadReceiptRepository() = default;

    // Inserts or advances the (message, user) receipt to at least `state`,
    // returning the resulting receipt. Never lowers an existing state.
    [[nodiscard]] virtual models::ReadReceipt upsert_state(std::int64_t message_id,
                                                           std::int64_t user_id,
                                                           models::ReceiptState state) = 0;

    // Marks every message in the conversation up to and including
    // `up_to_message_id` as read for `user_id` (excluding the user's own
    // messages), idempotently and in one statement.
    virtual void mark_conversation_read(std::int64_t conversation_id,
                                        std::int64_t user_id,
                                        std::int64_t up_to_message_id) = 0;

    [[nodiscard]] virtual std::vector<models::ReadReceipt> list_for_message(
        std::int64_t message_id) = 0;
};

}  // namespace rtc::repositories
