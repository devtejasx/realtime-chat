#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/models/reaction.hpp"
#include "rtc/realtime/event_broadcaster.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "rtc/repositories/reaction_repository.hpp"

namespace rtc::services {

// Business logic for message reactions. Validates the emoji against the allowed
// set, enforces conversation membership, persists, and broadcasts add/remove
// events. Used by both the REST controller and (potentially) WebSocket handlers.
class ReactionService {
public:
    ReactionService(repositories::IReactionRepository& reactions,
                    repositories::IMessageRepository& messages,
                    repositories::IConversationRepository& conversations,
                    realtime::IEventBroadcaster& broadcaster) noexcept
        : reactions_(reactions),
          messages_(messages),
          conversations_(conversations),
          broadcaster_(broadcaster) {}

    // Adds or changes the actor's reaction; broadcasts reaction.added.
    [[nodiscard]] models::Reaction react(std::int64_t actor_id, std::int64_t message_id,
                                         std::string_view emoji);

    // Removes the actor's reaction; broadcasts reaction.removed.
    void unreact(std::int64_t actor_id, std::int64_t message_id);

    [[nodiscard]] std::vector<models::Reaction> list(std::int64_t actor_id,
                                                     std::int64_t message_id);

private:
    // Ensures the message exists and the actor participates in its conversation;
    // returns the conversation id for fan-out.
    [[nodiscard]] std::int64_t authorize(std::int64_t actor_id, std::int64_t message_id);

    repositories::IReactionRepository& reactions_;
    repositories::IMessageRepository& messages_;
    repositories::IConversationRepository& conversations_;
    realtime::IEventBroadcaster& broadcaster_;
};

}  // namespace rtc::services
