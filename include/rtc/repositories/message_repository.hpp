#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/message.hpp"

namespace rtc::repositories {

// Parameters for inserting a message. Content is already validated/trimmed by
// the service layer.
struct NewMessage {
    std::int64_t conversation_id = 0;
    std::int64_t sender_id = 0;
    std::string content;
    models::MessageType type = models::MessageType::kText;
};

// Filter for listing/searching messages. `conversation_id` is required; the
// optional fields enable indexed filtering (sender) and full-text search
// (keyword). Cursor bounds come from the Pagination passed alongside.
struct MessageFilter {
    std::int64_t conversation_id = 0;
    std::optional<std::int64_t> sender_id;
    std::optional<std::string> keyword;
};

// Persistence boundary for messages.
class IMessageRepository {
  public:
    virtual ~IMessageRepository() = default;

    // Inserts a message and atomically advances the conversation's
    // last_message_at, returning the stored row.
    [[nodiscard]] virtual models::Message create(const NewMessage& input) = 0;

    [[nodiscard]] virtual std::optional<models::Message> find_by_id(std::int64_t id) = 0;

    // Lists messages matching the filter, newest-first, with keyset/offset
    // pagination. When a keyword is present, results are ranked by text match
    // and exclude deleted messages.
    [[nodiscard]] virtual std::vector<models::Message> list(const MessageFilter& filter,
                                                            const dto::Pagination& page) = 0;

    // Edits content and stamps edited_at. Throws NotFoundException if the
    // message is missing or already deleted.
    [[nodiscard]] virtual models::Message update_content(std::int64_t id,
                                                         std::string_view content) = 0;

    // Soft-deletes (sets deleted_at). Throws NotFoundException if missing or
    // already deleted. Returns the updated row for broadcasting.
    [[nodiscard]] virtual models::Message soft_delete(std::int64_t id) = 0;
};

}  // namespace rtc::repositories
