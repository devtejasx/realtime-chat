#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rtc/jobs/background_executor.hpp"
#include "rtc/media/image_processor.hpp"
#include "rtc/metrics/metrics_registry.hpp"
#include "rtc/models/attachment.hpp"
#include "rtc/repositories/attachment_repository.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "rtc/storage/file_storage.hpp"

namespace rtc::services {

// Business logic for file uploads and attachments.
//
// Enforces size and MIME allow-list rules, stores bytes through the pluggable
// IFileStorage backend, records metadata, and schedules image thumbnailing on
// the background executor (never on the request thread). Download authorization
// mirrors messaging visibility: the owner always, plus participants of the
// conversation once the attachment is linked to a message.
class AttachmentService {
public:
    struct Options {
        std::int64_t max_upload_bytes = 25 * 1024 * 1024;
        int thumbnail_max_dimension = 256;
    };

    struct UploadInput {
        std::int64_t owner_id = 0;
        std::string filename;
        std::string declared_content_type;
        std::string bytes;
    };

    struct Download {
        models::Attachment meta;
        std::string bytes;
        std::string content_type;
    };

    AttachmentService(repositories::IAttachmentRepository& attachments,
                      repositories::IMessageRepository& messages,
                      repositories::IConversationRepository& conversations,
                      storage::IFileStorage& storage, media::IImageProcessor& image_processor,
                      jobs::BackgroundExecutor& executor, metrics::MetricsRegistry& metrics,
                      Options options) noexcept
        : attachments_(attachments),
          messages_(messages),
          conversations_(conversations),
          storage_(storage),
          image_processor_(image_processor),
          executor_(executor),
          metrics_(metrics),
          options_(options) {}

    // Validates and stores an upload, returning its metadata. Schedules
    // thumbnail generation for images asynchronously.
    [[nodiscard]] models::Attachment upload(UploadInput input);

    // Returns metadata the actor is allowed to see (throws otherwise).
    [[nodiscard]] models::Attachment get_metadata(std::int64_t actor_id, std::int64_t id);

    // Returns the object's bytes (or its thumbnail) for an authorized actor.
    [[nodiscard]] Download download(std::int64_t actor_id, std::int64_t id, bool thumbnail);

    // Deletes an attachment (owner only), removing its bytes and thumbnail.
    void remove(std::int64_t actor_id, std::int64_t id);

    // Links uploaded attachments to a message (used by MessageService::send).
    std::size_t link_to_message(std::int64_t owner_id,
                                const std::vector<std::int64_t>& attachment_ids,
                                std::int64_t message_id);

    // Fetches a message's attachments (for message serialisation).
    [[nodiscard]] std::vector<models::Attachment> for_message(std::int64_t message_id);

private:
    [[nodiscard]] models::Attachment require_attachment(std::int64_t id);
    void authorize_view(std::int64_t actor_id, const models::Attachment& attachment);
    void schedule_image_processing(std::int64_t attachment_id, std::string bytes,
                                   std::string content_type, std::string base_key);

    repositories::IAttachmentRepository& attachments_;
    repositories::IMessageRepository& messages_;
    repositories::IConversationRepository& conversations_;
    storage::IFileStorage& storage_;
    media::IImageProcessor& image_processor_;
    jobs::BackgroundExecutor& executor_;
    metrics::MetricsRegistry& metrics_;
    Options options_;
};

}  // namespace rtc::services
