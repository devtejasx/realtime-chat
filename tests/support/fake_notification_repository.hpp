#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

#include "rtc/models/notification.hpp"
#include "rtc/repositories/notification_repository.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::testing {

// In-memory INotificationRepository.
class FakeNotificationRepository final : public repositories::INotificationRepository {
public:
    models::Notification create(std::int64_t user_id, models::NotificationType type,
                                const nlohmann::json& payload) override {
        models::Notification n;
        n.id = next_id_++;
        n.user_id = user_id;
        n.type = type;
        n.payload = payload;
        n.created_at = utils::now();
        notifications_.push_back(n);
        return n;
    }

    std::vector<models::Notification> list_for_user(std::int64_t user_id, const dto::Pagination&,
                                                    bool unread_only) override {
        std::vector<models::Notification> out;
        for (const auto& n : notifications_) {
            if (n.user_id != user_id) continue;
            if (unread_only && n.is_read()) continue;
            out.push_back(n);
        }
        return out;
    }

    std::int64_t unread_count(std::int64_t user_id) override {
        std::int64_t count = 0;
        for (const auto& n : notifications_)
            if (n.user_id == user_id && !n.is_read()) ++count;
        return count;
    }

    bool mark_read(std::int64_t id, std::int64_t user_id) override {
        for (auto& n : notifications_)
            if (n.id == id && n.user_id == user_id && !n.is_read()) {
                n.read_at = utils::now();
                return true;
            }
        return false;
    }

    std::int64_t mark_all_read(std::int64_t user_id) override {
        std::int64_t count = 0;
        for (auto& n : notifications_)
            if (n.user_id == user_id && !n.is_read()) {
                n.read_at = utils::now();
                ++count;
            }
        return count;
    }

    bool remove(std::int64_t id, std::int64_t user_id) override {
        const auto before = notifications_.size();
        notifications_.erase(
            std::remove_if(notifications_.begin(), notifications_.end(),
                           [&](const auto& n) { return n.id == id && n.user_id == user_id; }),
            notifications_.end());
        return notifications_.size() != before;
    }

    [[nodiscard]] std::size_t count() const noexcept { return notifications_.size(); }

private:
    std::vector<models::Notification> notifications_;
    std::int64_t next_id_ = 1;
};

}  // namespace rtc::testing
