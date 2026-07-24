#include "rtc/dto/read_receipt_dto.hpp"

#include <string>

#include "rtc/utils/time.hpp"

namespace rtc::dto {

ReceiptResponse ReceiptResponse::from(const models::ReadReceipt& receipt) {
    return ReceiptResponse{
        .message_id = receipt.message_id,
        .user_id = receipt.user_id,
        .state = std::string(models::to_string(receipt.state)),
        .updated_at = utils::to_iso8601(receipt.updated_at),
    };
}

nlohmann::json ReceiptResponse::to_json() const {
    return nlohmann::json{
        {"message_id", message_id},
        {"user_id", user_id},
        {"state", state},
        {"updated_at", updated_at},
    };
}

}  // namespace rtc::dto
