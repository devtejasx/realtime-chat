#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "rtc/models/read_receipt.hpp"

namespace rtc::dto {

// Outbound representation of a read receipt.
struct ReceiptResponse {
    std::int64_t message_id = 0;
    std::int64_t user_id = 0;
    std::string state;
    std::string updated_at;

    [[nodiscard]] static ReceiptResponse from(const models::ReadReceipt& receipt);
    [[nodiscard]] nlohmann::json to_json() const;
};

}  // namespace rtc::dto
