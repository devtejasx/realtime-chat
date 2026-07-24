#include "rtc/realtime/room_manager.hpp"

namespace rtc::realtime {

void RoomManager::join(std::int64_t conversation_id, crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    rooms_[conversation_id].insert(conn);
    membership_[conn].insert(conversation_id);
}

void RoomManager::join_many(const std::vector<std::int64_t>& conversation_ids,
                            crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& joined = membership_[conn];
    for (const std::int64_t id : conversation_ids) {
        rooms_[id].insert(conn);
        joined.insert(id);
    }
}

void RoomManager::leave(std::int64_t conversation_id, crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = rooms_.find(conversation_id); it != rooms_.end()) {
        it->second.erase(conn);
        if (it->second.empty()) {
            rooms_.erase(it);
        }
    }
    if (const auto it = membership_.find(conn); it != membership_.end()) {
        it->second.erase(conversation_id);
        if (it->second.empty()) {
            membership_.erase(it);
        }
    }
}

void RoomManager::leave_all(crow::websocket::connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto member_it = membership_.find(conn);
    if (member_it == membership_.end()) {
        return;
    }
    for (const std::int64_t id : member_it->second) {
        if (const auto room_it = rooms_.find(id); room_it != rooms_.end()) {
            room_it->second.erase(conn);
            if (room_it->second.empty()) {
                rooms_.erase(room_it);
            }
        }
    }
    membership_.erase(member_it);
}

std::vector<crow::websocket::connection*> RoomManager::connections_in_room(
    std::int64_t conversation_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<crow::websocket::connection*> out;
    if (const auto it = rooms_.find(conversation_id); it != rooms_.end()) {
        out.reserve(it->second.size());
        for (auto* conn : it->second) {
            out.push_back(conn);
        }
    }
    return out;
}

std::size_t RoomManager::room_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}

}  // namespace rtc::realtime
