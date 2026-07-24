#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "rtc/utils/time.hpp"

namespace rtc::services {

// Tracks user online/offline state and last-seen time in memory.
//
// A user may have several concurrent sessions (tabs, devices); presence is
// reference-counted so "offline" is reported only when the last session closes.
// All state is guarded by a mutex — the class is safe to call from many
// WebSocket I/O threads. Broadcasting presence changes is the caller's job
// (the dispatcher decides who is interested); this service owns only the state.
class PresenceService {
public:
    // Registers a new session for the user. Returns true if this made the user
    // transition offline -> online (i.e. their first live session).
    [[nodiscard]] bool on_connect(std::int64_t user_id);

    // Deregisters a session. Returns true if this made the user transition
    // online -> offline (their last session closed); last_seen is stamped then.
    [[nodiscard]] bool on_disconnect(std::int64_t user_id);

    [[nodiscard]] bool is_online(std::int64_t user_id) const;

    // Last time the user went fully offline, if ever recorded.
    [[nodiscard]] std::optional<utils::TimePoint> last_seen(std::int64_t user_id) const;

    [[nodiscard]] std::size_t online_count() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, int> sessions_;          // user_id -> live session count
    std::unordered_map<std::int64_t, utils::TimePoint> last_seen_;
};

}  // namespace rtc::services
