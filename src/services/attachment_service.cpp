#include "rtc/services/attachment_service.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "rtc/errors/exceptions.hpp"
#include "rtc/media/mime.hpp"
#include "rtc/utils/hash.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::services {
namespace {

// Derives an effective, allowed content type, cross-checking the declared type
// against the filename extension to resist spoofing.
[[nodiscard]] std::string resolve_content_type(const std::string& declared,
                                               const std::string& filename) {
    if (!declared.empty() && media::is_allowed_upload_type(declared)) {
        return declared;
    }
    // Fall back to the extension-inferred type.
    const std::string inferred = media::content_type_for_extension(filename);
    if (!inferred.empty() && media::is_allowed_upload_type(inferred)) {
        return inferred;
    }
    throw rtc::errors::UnsupportedMediaTypeException("Unsupported or disallowed file type",
                                                     "content_type=" + declared);
}

[[nodiscard]] std::string extension_of(const std::string& filename) {
    const std::filesystem::path p(filename);
    return p.has_extension() ? p.extension().string() : std::string{};
}

}  // namespace

models::Attachment AttachmentService::upload(UploadInput input) {
    if (input.bytes.empty()) {
        throw rtc::errors::ValidationException("Uploaded file is empty");
    }
    if (static_cast<std::int64_t>(input.bytes.size()) > options_.max_upload_bytes) {
        throw rtc::errors::PayloadTooLargeException(
            "File exceeds the maximum allowed size",
            "max_bytes=" + std::to_string(options_.max_upload_bytes));
    }

    const std::string content_type =
        resolve_content_type(input.declared_content_type, input.filename);
    const models::AttachmentKind kind = media::classify(content_type);

    // Opaque, collision-resistant key namespaced by owner; preserves extension.
    const std::string key = std::to_string(input.owner_id) + "/" +
                            utils::generate_hex_token(16) + extension_of(input.filename);

    const auto stored = storage_.put(key, content_type, input.bytes);

    repositories::NewAttachment record;
    record.owner_id = input.owner_id;
    record.storage_backend = stored.backend;
    record.storage_key = stored.key;
    record.original_filename = input.filename;
    record.content_type = content_type;
    record.kind = kind;
    record.byte_size = stored.size;
    record.checksum = utils::sha256_hex(input.bytes);

    const models::Attachment attachment = attachments_.create(record);

    metrics_.increment("rtc_uploads_total");
    metrics_.observe("rtc_upload_bytes", static_cast<double>(stored.size));

    if (kind == models::AttachmentKind::kImage && image_processor_.supports(content_type)) {
        schedule_image_processing(attachment.id, input.bytes, content_type, stored.key);
    }
    return attachment;
}

void AttachmentService::schedule_image_processing(std::int64_t attachment_id, std::string bytes,
                                                  std::string content_type,
                                                  std::string base_key) {
    // Runs off the request thread: probe dimensions, generate a thumbnail, and
    // persist both. Failures are logged by the executor and never surfaced to
    // the uploader.
    executor_.submit([this, attachment_id, bytes = std::move(bytes),
                      content_type = std::move(content_type), base_key = std::move(base_key)] {
        std::optional<int> width, height;
        if (const auto info = image_processor_.probe(bytes)) {
            width = info->width;
            height = info->height;
        }
        std::optional<std::string> thumbnail_key;
        if (auto thumb = image_processor_.make_thumbnail(bytes, content_type,
                                                         options_.thumbnail_max_dimension)) {
            const std::string tkey = base_key + ".thumb";
            storage_.put(tkey, thumb->content_type, thumb->bytes);
            thumbnail_key = tkey;
        }
        if (width || height || thumbnail_key) {
            attachments_.update_media_meta(attachment_id, width, height, thumbnail_key);
        }
    });
}

models::Attachment AttachmentService::require_attachment(std::int64_t id) {
    auto attachment = attachments_.find_by_id(id);
    if (!attachment) {
        throw rtc::errors::NotFoundException("Attachment not found");
    }
    return *attachment;
}

void AttachmentService::authorize_view(std::int64_t actor_id,
                                       const models::Attachment& attachment) {
    if (attachment.owner_id == actor_id) {
        return;
    }
    // Linked to a message: participants of that conversation may view it.
    if (attachment.message_id) {
        if (const auto message = messages_.find_by_id(*attachment.message_id)) {
            if (conversations_.is_participant(message->conversation_id, actor_id)) {
                return;
            }
        }
    }
    throw rtc::errors::NotFoundException("Attachment not found");
}

models::Attachment AttachmentService::get_metadata(std::int64_t actor_id, std::int64_t id) {
    const auto attachment = require_attachment(id);
    authorize_view(actor_id, attachment);
    return attachment;
}

AttachmentService::Download AttachmentService::download(std::int64_t actor_id, std::int64_t id,
                                                        bool thumbnail) {
    const auto attachment = require_attachment(id);
    authorize_view(actor_id, attachment);

    const std::string key =
        (thumbnail && attachment.thumbnail_key) ? *attachment.thumbnail_key
                                                : attachment.storage_key;
    auto bytes = storage_.get(key);
    if (!bytes) {
        throw rtc::errors::NotFoundException("Attachment content not found");
    }
    metrics_.increment("rtc_downloads_total");
    return Download{attachment, std::move(*bytes), attachment.content_type};
}

void AttachmentService::remove(std::int64_t actor_id, std::int64_t id) {
    const auto attachment = require_attachment(id);
    if (attachment.owner_id != actor_id) {
        throw rtc::errors::AuthorizationException("Only the owner may delete this attachment");
    }
    storage_.remove(attachment.storage_key);
    if (attachment.thumbnail_key) {
        storage_.remove(*attachment.thumbnail_key);
    }
    attachments_.remove(id);
}

std::size_t AttachmentService::link_to_message(std::int64_t owner_id,
                                               const std::vector<std::int64_t>& attachment_ids,
                                               std::int64_t message_id) {
    return attachments_.link_to_message(attachment_ids, message_id, owner_id);
}

std::vector<std::int64_t> AttachmentService::attachment_ids_for(std::int64_t message_id) {
    std::vector<std::int64_t> ids;
    for (const auto& attachment : attachments_.list_for_message(message_id)) {
        ids.push_back(attachment.id);
    }
    return ids;
}

std::vector<models::Attachment> AttachmentService::for_message(std::int64_t message_id) {
    return attachments_.list_for_message(message_id);
}

}  // namespace rtc::services
