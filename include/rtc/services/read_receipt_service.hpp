#pragma once

#include <cstdint>

#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "rtc/repositories/read_receipt_repository.hpp"

namespace rtc::services {

// Business logic for delivery/read receipts. Persists receipt state, keeps the
// per-participant read marker in sync, and broadcasts updates. Invoked by the
// WebSocket handlers (mark_delivered / mark_read events) and reusable by REST.
class ReadReceiptService {
public:
    ReadReceiptService(repositories::IReadReceiptRepository& receipts,
                       repositories::IConversationRepository& conversations,
                       repositories::IMessageRepository& messages,
                       realtime::IEventBroadcaster& broadcaster) noexcept
        : receipts_(receipts),
          conversations_(conversations),
          messages_(messages),
          broadcaster_(broadcaster) {}

    // Records that `actor_id` received `message_id` (state -> delivered) and
    // broadcasts the receipt to the conversation.
    void mark_delivered(std::int64_t actor_id, std::int64_t message_id);

    // Marks all messages in `conversation_id` up to `up_to_message_id` as read
    // for `actor_id`, advances the read marker, and broadcasts a read update.
    void mark_read(std::int64_t actor_id, std::int64_t conversation_id,
                   std::int64_t up_to_message_id);

private:
    repositories::IReadReceiptRepository& receipts_;
    repositories::IConversationRepository& conversations_;
    repositories::IMessageRepository& messages_;
    realtime::IEventBroadcaster& broadcaster_;
};

}  // namespace rtc::services
