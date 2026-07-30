#include "rtc/realtime/session_manager.hpp"

#include <utility>

namespace rtc::realtime {

std::shared_ptr<Session> SessionManager::add(crow::websocket::connection* conn,
                                             std::int64_t user_id, std::string username,
                                             protocol::Version version) {
    auto session = std::make_shared<Session>(conn, user_id, std::move(username), 0, version);
    std::lock_guard<std::mutex> lock(mutex_);
    by_connection_[conn] = session;
    by_user_[user_id].insert(conn);
    return session;
}

std::shared_ptr<Session> SessionManager::remove(crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_connection_.find(conn);
    if (it == by_connection_.end()) {
        return nullptr;
    }
    auto session = it->second;
    by_connection_.erase(it);

    const auto user_it = by_user_.find(session->user_id);
    if (user_it != by_user_.end()) {
        user_it->second.erase(conn);
        if (user_it->second.empty()) {
            by_user_.erase(user_it);
        }
    }
    return session;
}

std::shared_ptr<Session> SessionManager::get(crow::websocket::connection* conn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_connection_.find(conn);
    return it == by_connection_.end() ? nullptr : it->second;
}

std::vector<crow::websocket::connection*> SessionManager::connections_for_user(
    std::int64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<crow::websocket::connection*> out;
    const auto it = by_user_.find(user_id);
    if (it != by_user_.end()) {
        out.reserve(it->second.size());
        for (auto* conn : it->second) {
            out.push_back(conn);
        }
    }
    return out;
}

std::vector<std::shared_ptr<Session>> SessionManager::sessions_for_user(
    std::int64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Session>> out;
    const auto it = by_user_.find(user_id);
    if (it == by_user_.end()) {
        return out;
    }
    out.reserve(it->second.size());
    for (auto* conn : it->second) {
        const auto session_it = by_connection_.find(conn);
        if (session_it != by_connection_.end()) {
            out.push_back(session_it->second);
        }
    }
    return out;
}

std::vector<std::shared_ptr<Session>> SessionManager::sessions_for(
    const std::vector<crow::websocket::connection*>& connections) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Session>> out;
    out.reserve(connections.size());
    for (auto* conn : connections) {
        const auto it = by_connection_.find(conn);
        if (it != by_connection_.end()) {
            out.push_back(it->second);
        }
    }
    return out;
}

std::vector<std::shared_ptr<Session>> SessionManager::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Session>> out;
    out.reserve(by_connection_.size());
    for (const auto& [conn, session] : by_connection_) {
        out.push_back(session);
    }
    return out;
}

void SessionManager::touch(crow::websocket::connection* conn, std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = by_connection_.find(conn);
    if (it != by_connection_.end()) {
        it->second->last_activity_ms.store(now_ms, std::memory_order_relaxed);
    }
}

std::size_t SessionManager::session_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return by_connection_.size();
}

}  // namespace rtc::realtime
