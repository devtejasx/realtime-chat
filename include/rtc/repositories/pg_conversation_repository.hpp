#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/conversation_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IConversationRepository. Contains only SQL and row mapping;
// transactions come from BaseRepository.
class PgConversationRepository final : public database::BaseRepository,
                                       public IConversationRepository {
  public:
    explicit PgConversationRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Conversation create_or_get_direct(std::int64_t user_a,
                                                            std::int64_t user_b) override;
    [[nodiscard]] models::Conversation create_group(
        std::int64_t owner_id,
        std::string_view name,
        const std::vector<std::int64_t>& member_ids) override;

    [[nodiscard]] std::optional<models::Conversation> find_by_id(std::int64_t id) override;
    [[nodiscard]] std::vector<models::Conversation> list_for_user(
        std::int64_t user_id, const dto::Pagination& page) override;
    [[nodiscard]] std::vector<models::ConversationParticipant> list_participants(
        std::int64_t conversation_id) override;
    [[nodiscard]] std::vector<std::int64_t> list_participant_ids(
        std::int64_t conversation_id) override;
    [[nodiscard]] std::vector<std::int64_t> list_conversation_ids(std::int64_t user_id) override;
    [[nodiscard]] std::vector<std::int64_t> list_peer_ids(std::int64_t user_id) override;
    [[nodiscard]] std::optional<models::ConversationParticipant> find_participant(
        std::int64_t conversation_id, std::int64_t user_id) override;
    [[nodiscard]] bool is_participant(std::int64_t conversation_id, std::int64_t user_id) override;

    void add_participant(std::int64_t conversation_id,
                         std::int64_t user_id,
                         models::ParticipantRole role) override;
    void remove_participant(std::int64_t conversation_id, std::int64_t user_id) override;
    void rename(std::int64_t conversation_id, std::string_view name) override;
    void transfer_ownership(std::int64_t conversation_id, std::int64_t new_owner_id) override;
    void remove(std::int64_t conversation_id) override;
    void update_last_read(std::int64_t conversation_id,
                          std::int64_t user_id,
                          std::int64_t message_id) override;
};

}  // namespace rtc::repositories
