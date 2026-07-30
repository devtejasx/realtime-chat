#pragma once

#include <cstdint>
#include <vector>

namespace rtc::services {

// Narrow seam that lets MessageService attach uploaded files to a message
// without depending on the whole AttachmentService (and without a dependency
// cycle). AttachmentService implements it; a null implementation is used when
// attachments are irrelevant (unit tests, minimal builds).
class IAttachmentLinker {
  public:
    virtual ~IAttachmentLinker() = default;

    // Links the owner's unattached uploads to a message; returns count linked.
    virtual std::size_t link_to_message(std::int64_t owner_id,
                                        const std::vector<std::int64_t>& attachment_ids,
                                        std::int64_t message_id) = 0;

    // Returns the ids of attachments currently linked to a message.
    [[nodiscard]] virtual std::vector<std::int64_t> attachment_ids_for(std::int64_t message_id) = 0;
};

// No-op linker: messages carry no attachments.
class NullAttachmentLinker final : public IAttachmentLinker {
  public:
    std::size_t link_to_message(std::int64_t,
                                const std::vector<std::int64_t>&,
                                std::int64_t) override {
        return 0;
    }
    [[nodiscard]] std::vector<std::int64_t> attachment_ids_for(std::int64_t) override { return {}; }
};

}  // namespace rtc::services
