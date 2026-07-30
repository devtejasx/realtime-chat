#pragma once

#include <cstdint>
#include <vector>

#include "rtc/models/read_receipt.hpp"
#include "rtc/repositories/read_receipt_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory IReadReceiptRepository preserving monotonic state advancement.
class FakeReadReceiptRepository final : public repositories::IReadReceiptRepository {
  public:
    models::ReadReceipt upsert_state(std::int64_t message_id,
                                     std::int64_t user_id,
                                     models::ReceiptState state) override {
        for (auto& r : receipts_) {
            if (r.message_id == message_id && r.user_id == user_id) {
                if (models::rank(state) > models::rank(r.state)) {
                    r.state = state;
                }
                r.updated_at = utils::now();
                return r;
            }
        }
        models::ReadReceipt r;
        r.id = next_id_++;
        r.message_id = message_id;
        r.user_id = user_id;
        r.state = state;
        r.updated_at = utils::now();
        receipts_.push_back(r);
        return r;
    }

    void mark_conversation_read(std::int64_t,
                                std::int64_t user_id,
                                std::int64_t up_to_message_id) override {
        last_mark_user_ = user_id;
        last_mark_up_to_ = up_to_message_id;
    }

    std::vector<models::ReadReceipt> list_for_message(std::int64_t message_id) override {
        std::vector<models::ReadReceipt> out;
        for (const auto& r : receipts_)
            if (r.message_id == message_id)
                out.push_back(r);
        return out;
    }

    [[nodiscard]] std::int64_t last_mark_user() const noexcept { return last_mark_user_; }
    [[nodiscard]] std::int64_t last_mark_up_to() const noexcept { return last_mark_up_to_; }

  private:
    std::vector<models::ReadReceipt> receipts_;
    std::int64_t next_id_ = 1;
    std::int64_t last_mark_user_ = 0;
    std::int64_t last_mark_up_to_ = 0;
};

}  // namespace rtc::testing
