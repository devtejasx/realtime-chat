#include "rtc/services/read_receipt_service.hpp"

#include "rtc/dto/read_receipt_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/models/read_receipt.hpp"
#include "rtc/realtime/events.hpp"

namespace rtc::services {

void ReadReceiptService::mark_delivered(std::int64_t actor_id, std::int64_t message_id) {
    const auto message = messages_.find_by_id(message_id);
    if (!message) {
        throw errors::NotFoundException("Message not found");
    }
    if (!conversations_.is_participant(message->conversation_id, actor_id)) {
        throw errors::NotFoundException("Conversation not found");
    }

    const auto receipt =
        receipts_.upsert_state(message_id, actor_id, models::ReceiptState::kDelivered);

    broadcaster_.publish(conversations_.list_participant_ids(message->conversation_id),
                         realtime::events::kReceiptUpdate,
                         dto::ReceiptResponse::from(receipt).to_json());
}

void ReadReceiptService::mark_read(std::int64_t actor_id,
                                   std::int64_t conversation_id,
                                   std::int64_t up_to_message_id) {
    if (!conversations_.is_participant(conversation_id, actor_id)) {
        throw errors::NotFoundException("Conversation not found");
    }

    // Persist read state for the backlog, then advance the participant marker.
    receipts_.mark_conversation_read(conversation_id, actor_id, up_to_message_id);
    conversations_.update_last_read(conversation_id, actor_id, up_to_message_id);

    broadcaster_.publish(conversations_.list_participant_ids(conversation_id),
                         realtime::events::kReadUpdate,
                         nlohmann::json{{"conversation_id", conversation_id},
                                        {"user_id", actor_id},
                                        {"up_to_message_id", up_to_message_id}});
}

}  // namespace rtc::services
