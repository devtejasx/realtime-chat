#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "rtc/utils/time.hpp"

namespace rtc::models {

// Per-user delivery state of a message. Advances monotonically:
// sent -> delivered -> read. The ordering is meaningful (see rank()).
enum class ReceiptState {
    kSent,
    kDelivered,
    kRead,
};

[[nodiscard]] constexpr std::string_view to_string(ReceiptState state) noexcept {
    switch (state) {
        case ReceiptState::kSent:
            return "sent";
        case ReceiptState::kDelivered:
            return "delivered";
        case ReceiptState::kRead:
            return "read";
    }
    return "sent";
}

[[nodiscard]] constexpr std::optional<ReceiptState> receipt_state_from_string(
    std::string_view value) noexcept {
    if (value == "sent")
        return ReceiptState::kSent;
    if (value == "delivered")
        return ReceiptState::kDelivered;
    if (value == "read")
        return ReceiptState::kRead;
    return std::nullopt;
}

// Monotonic rank so state can only advance, never regress.
[[nodiscard]] constexpr int rank(ReceiptState state) noexcept {
    switch (state) {
        case ReceiptState::kSent:
            return 0;
        case ReceiptState::kDelivered:
            return 1;
        case ReceiptState::kRead:
            return 2;
    }
    return 0;
}

// Persistent read-receipt entity (row in `read_receipts`).
struct ReadReceipt {
    std::int64_t id = 0;
    std::int64_t message_id = 0;
    std::int64_t user_id = 0;
    ReceiptState state = ReceiptState::kSent;
    utils::TimePoint updated_at{};
};

}  // namespace rtc::models
