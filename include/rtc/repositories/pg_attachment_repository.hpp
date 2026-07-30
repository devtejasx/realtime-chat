#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "rtc/database/base_repository.hpp"
#include "rtc/repositories/attachment_repository.hpp"

namespace rtc::repositories {

// PostgreSQL-backed IAttachmentRepository.
class PgAttachmentRepository final : public database::BaseRepository, public IAttachmentRepository {
  public:
    explicit PgAttachmentRepository(database::ConnectionPool& pool) noexcept
        : database::BaseRepository(pool) {}

    [[nodiscard]] models::Attachment create(const NewAttachment& input) override;
    [[nodiscard]] std::optional<models::Attachment> find_by_id(std::int64_t id) override;
    [[nodiscard]] std::vector<models::Attachment> list_for_message(
        std::int64_t message_id) override;
    std::size_t link_to_message(const std::vector<std::int64_t>& attachment_ids,
                                std::int64_t message_id,
                                std::int64_t owner_id) override;
    void update_media_meta(std::int64_t id,
                           std::optional<int> width,
                           std::optional<int> height,
                           std::optional<std::string> thumbnail_key) override;
    bool remove(std::int64_t id) override;
};

}  // namespace rtc::repositories
