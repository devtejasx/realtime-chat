#include "rtc/services/presence_service.hpp"

#include <chrono>
#include <utility>

namespace rtc::services {
namespace {

[[nodiscard]] std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

bool PresenceService::online_remotely_locked(std::int64_t user_id) const {
    for (const auto& [node, users] : remote_) {
        if (users.count(user_id) != 0U) {
            return true;
        }
    }
    return false;
}

bool PresenceService::online_anywhere_locked(std::int64_t user_id) const {
    const auto it = sessions_.find(user_id);
    if (it != sessions_.end() && it->second > 0) {
        return true;
    }
    return online_remotely_locked(user_id);
}

bool PresenceService::on_connect(std::int64_t user_id) {
    bool became_online = false;
    bool announce = false;
    PresenceDelta delta;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool was_online = online_anywhere_locked(user_id);
        const int previous = sessions_[user_id];
        sessions_[user_id] = previous + 1;

        // Global transition: nobody anywhere had this user, and now we do.
        became_online = !was_online;
        // Local transition: this node's first session for the user. Peers are
        // told about *that*, independently of the global answer — their view of
        // this node must stay accurate even when another node already had the
        // user online.
        announce = previous == 0;
        delta = PresenceDelta{.user_id = user_id, .online = true, .at_ms = now_ms()};
    }

    // Published outside the lock: the publisher reaches the network, and holding
    // a mutex that every WebSocket thread contends on across a socket write is
    // how a slow Redis turns into a stalled event loop.
    if (announce) {
        publisher_->publish_presence(delta);
    }
    return became_online;
}

bool PresenceService::on_disconnect(std::int64_t user_id) {
    bool became_offline = false;
    bool announce = false;
    PresenceDelta delta;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = sessions_.find(user_id);
        if (it == sessions_.end() || it->second == 0) {
            return false;
        }
        it->second -= 1;
        if (it->second > 0) {
            // Still other sessions on this node; nothing changed anywhere.
            return false;
        }

        sessions_.erase(it);
        announce = true;
        // Only offline if no *other* instance still holds a session. This is the
        // check whose absence produced a false "offline" for a user connected to
        // two replicas.
        became_offline = !online_remotely_locked(user_id);
        if (became_offline) {
            last_seen_[user_id] = utils::now();
        }
        delta = PresenceDelta{.user_id = user_id, .online = false, .at_ms = now_ms()};
    }

    if (announce) {
        publisher_->publish_presence(delta);
    }
    return became_offline;
}

void PresenceService::apply_remote(std::string_view node_id, const PresenceDelta& delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string node(node_id);
    if (delta.online) {
        remote_[node].insert(delta.user_id);
        return;
    }

    const auto node_it = remote_.find(node);
    if (node_it == remote_.end()) {
        return;
    }
    node_it->second.erase(delta.user_id);
    if (node_it->second.empty()) {
        // Keep the map proportional to nodes that actually hold sessions rather
        // than to every node ever heard from.
        remote_.erase(node_it);
    }

    // Stamp last-seen only once the user is gone from everywhere, so a tab
    // closing on one replica does not overwrite a live session's last-seen.
    if (!online_anywhere_locked(delta.user_id)) {
        last_seen_[delta.user_id] = utils::now();
    }
}

void PresenceService::forget_node(std::string_view node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_.erase(std::string(node_id));
}

bool PresenceService::is_online(std::int64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return online_anywhere_locked(user_id);
}

bool PresenceService::is_online_locally(std::int64_t user_id) const {
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
    if (remote_.empty()) {
        return sessions_.size();
    }
    // Distinct users, not the sum: the same user connected to two replicas is
    // one online user.
    std::unordered_set<std::int64_t> distinct;
    distinct.reserve(sessions_.size());
    for (const auto& [user_id, count] : sessions_) {
        if (count > 0) {
            distinct.insert(user_id);
        }
    }
    for (const auto& [node, users] : remote_) {
        distinct.insert(users.begin(), users.end());
    }
    return distinct.size();
}

std::size_t PresenceService::local_online_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

std::size_t PresenceService::known_peer_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return remote_.size();
}

}  // namespace rtc::services
