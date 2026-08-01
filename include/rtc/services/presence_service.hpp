#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "rtc/services/presence_publisher.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::services {

// Tracks user online/offline state and last-seen time.
//
// A user may have several concurrent sessions (tabs, devices), and on a
// multi-instance deployment those sessions may be spread across replicas. State
// is therefore reference-counted per node: this instance's own live sessions,
// plus the set of users each *other* instance has reported as online.
//
// Why the remote half exists
// --------------------------
// Counting only local sessions is not merely incomplete, it is wrong in a way
// that produces visibly incorrect output. With a user connected to both A and B:
//
//   - B sees its own connection as the user's first and announces "online",
//     duplicating an announcement A already made.
//   - When that tab closes, B sees its own disconnect as the user's last and
//     announces "offline" — while the user is still connected to A.
//
// The false "offline" is the serious one: peers mark the user away while they
// are sitting in the conversation. So on_connect/on_disconnect report *global*
// transitions, computed across every node, and callers emit a client-facing
// frame only when the global answer actually changed.
//
// Staleness: if a node dies without announcing its users offline, its entries
// linger and those users read as online. forget_node() exists for that; the
// bounded fallback is that a reconnecting client re-announces, so active users
// self-heal. Presence is soft state and this is the accepted trade — the
// alternative, a per-user TTL key in Redis refreshed on every heartbeat, buys
// accuracy for a write per heartbeat per user.
//
// All state is guarded by one mutex; safe to call from many WebSocket I/O
// threads and from the cluster bus's subscriber thread. Broadcasting to clients
// remains the caller's job (the dispatcher decides who is interested); this
// service owns only the state and its cross-instance propagation.
class PresenceService {
  public:
    // Registers a live session on this instance. Returns true when the user was
    // offline *everywhere* and is now online — i.e. when a client-facing
    // "online" frame is warranted.
    //
    // Announces the local delta to other instances when a publisher is wired.
    [[nodiscard]] bool on_connect(std::int64_t user_id);

    // Deregisters a session on this instance. Returns true when the user is now
    // offline *everywhere*; last_seen is stamped then.
    [[nodiscard]] bool on_disconnect(std::int64_t user_id);

    // Applies another instance's delta. Called from the cluster subscriber, and
    // deliberately does not re-publish: propagating a propagation would loop.
    void apply_remote(std::string_view node_id, const PresenceDelta& delta);

    // Drops everything a node reported, for use when an instance is known to be
    // gone so its users stop reading as online.
    void forget_node(std::string_view node_id);

    // True when this instance or any other reports a live session.
    [[nodiscard]] bool is_online(std::int64_t user_id) const;

    // True when *this* instance holds a live session. The distinction matters
    // for anything that must act only where the socket actually is.
    [[nodiscard]] bool is_online_locally(std::int64_t user_id) const;

    // Last time the user went fully offline, if ever recorded.
    [[nodiscard]] std::optional<utils::TimePoint> last_seen(std::int64_t user_id) const;

    // Distinct users online across the whole cluster.
    [[nodiscard]] std::size_t online_count() const;

    // Distinct users with a session on this instance.
    [[nodiscard]] std::size_t local_online_count() const;

    // Peer instances currently reporting anyone online. Surfaced for
    // diagnostics: zero on a multi-replica deployment means the bus is not
    // carrying presence.
    [[nodiscard]] std::size_t known_peer_count() const;

    void set_publisher(IPresencePublisher& publisher) noexcept { publisher_ = &publisher; }

  private:
    // Callers must hold mutex_.
    [[nodiscard]] bool online_anywhere_locked(std::int64_t user_id) const;
    [[nodiscard]] bool online_remotely_locked(std::int64_t user_id) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::int64_t, int> sessions_;  // user_id -> live sessions here
    std::unordered_map<std::int64_t, utils::TimePoint> last_seen_;
    // node_id -> users that node reports online. Keyed by node so a single
    // departure can be undone wholesale.
    std::unordered_map<std::string, std::unordered_set<std::int64_t>> remote_;
    IPresencePublisher* publisher_{&NullPresencePublisher::instance()};
};

}  // namespace rtc::services
