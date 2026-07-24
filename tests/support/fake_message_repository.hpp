#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/errors/exceptions.hpp"
#include "rtc/models/message.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory IMessageRepository for service unit tests. Implements the same
// filter semantics (sender, keyword substring, keyset bounds, newest-first).
class FakeMessageRepository final : public repositories::IMessageRepository {
public:
    models::Message create(const repositories::NewMessage& input) override {
        models::Message m;
        m.id = next_id_++;
        m.conversation_id = input.conversation_id;
        m.sender_id = input.sender_id;
        m.type = input.type;
        m.content = input.content;
        m.created_at = utils::now();
        m.updated_at = m.created_at;
        messages_.push_back(m);
        return m;
    }

    std::optional<models::Message> find_by_id(std::int64_t id) override {
        for (const auto& m : messages_)
            if (m.id == id) return m;
        return std::nullopt;
    }

    std::vector<models::Message> list(const repositories::MessageFilter& f,
                                      const dto::Pagination& page) override {
        std::vector<models::Message> out;
        for (const auto& m : messages_) {
            if (m.conversation_id != f.conversation_id) continue;
            if (f.sender_id && m.sender_id != *f.sender_id) continue;
            if (f.keyword) {
                if (m.is_deleted()) continue;
                if (m.content.find(*f.keyword) == std::string::npos) continue;
            }
            if (page.before_id && m.id >= *page.before_id) continue;
            if (page.after_id && m.id <= *page.after_id) continue;
            out.push_back(m);
        }
        std::sort(out.begin(), out.end(),
                  [](const auto& a, const auto& b) { return a.id > b.id; });
        if (static_cast<int>(out.size()) > page.limit) out.resize(page.limit);
        return out;
    }

    models::Message update_content(std::int64_t id, std::string_view content) override {
        for (auto& m : messages_) {
            if (m.id == id && !m.is_deleted()) {
                m.content = std::string(content);
                m.edited_at = utils::now();
                return m;
            }
        }
        throw rtc::errors::NotFoundException("Message not found or already deleted");
    }

    models::Message soft_delete(std::int64_t id) override {
        for (auto& m : messages_) {
            if (m.id == id && !m.is_deleted()) {
                m.deleted_at = utils::now();
                return m;
            }
        }
        throw rtc::errors::NotFoundException("Message not found or already deleted");
    }

    [[nodiscard]] std::size_t count() const noexcept { return messages_.size(); }

private:
    std::vector<models::Message> messages_;
    std::int64_t next_id_ = 1;
};

}  // namespace rtc::testing
