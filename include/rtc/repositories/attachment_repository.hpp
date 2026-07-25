#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtc/models/attachment.hpp"

namespace rtc::repositories {

// Parameters for inserting attachment metadata (bytes already stored).
struct NewAttachment {
    std::int64_t owner_id = 0;
    std::string storage_backend;
    std::string storage_key;
    std::string original_filename;
    std::string content_type;
    models::AttachmentKind kind = models::AttachmentKind::kOther;
    std::int64_t byte_size = 0;
    std::optional<std::string> checksum;
};

// Persistence boundary for attachments.
class IAttachmentRepository {
public:
    virtual ~IAttachmentRepository() = default;

    [[nodiscard]] virtual models::Attachment create(const NewAttachment& input) = 0;
    [[nodiscard]] virtual std::optional<models::Attachment> find_by_id(std::int64_t id) = 0;
    [[nodiscard]] virtual std::vector<models::Attachment> list_for_message(
        std::int64_t message_id) = 0;

    // Links previously-uploaded, still-unattached attachments owned by
    // `owner_id` to a message. Attachments already linked or owned by someone
    // else are ignored. Returns the number linked.
    virtual std::size_t link_to_message(const std::vector<std::int64_t>& attachment_ids,
                                        std::int64_t message_id, std::int64_t owner_id) = 0;

    // Records post-processing results (dimensions and/or a generated thumbnail).
    virtual void update_media_meta(std::int64_t id, std::optional<int> width,
                                   std::optional<int> height,
                                   std::optional<std::string> thumbnail_key) = 0;

    virtual bool remove(std::int64_t id) = 0;
};

}  // namespace rtc::repositories
