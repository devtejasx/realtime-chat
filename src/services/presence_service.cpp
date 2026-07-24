#include "rtc/services/presence_service.hpp"

namespace rtc::services {

bool PresenceService::on_connect(std::int64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int previous = sessions_[user_id];
    sessions_[user_id] = previous + 1;
    return previous == 0;  // transitioned to online
}

bool PresenceService::on_disconnect(std::int64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(user_id);
    if (it == sessions_.end() || it->second == 0) {
        return false;
    }
    it->second -= 1;
    if (it->second == 0) {
        sessions_.erase(it);
        last_seen_[user_id] = utils::now();
        return true;  // transitioned to offline
    }
    return false;
}

bool PresenceService::is_online(std::int64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(user_id);
    return it != sessions_.end() && it->second > 0;
}

std::optional<utils::TimePoint> PresenceService::last_seen(std::int64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = last_seen_.find(user_id);
    if (it == last_seen_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::size_t PresenceService::online_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

}  // namespace rtc::services
