#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rtc::realtime {

// Channels the cluster bus fans out over. Namespaced so a shared Redis instance
// can host other applications without collision.
namespace cluster_channels {
// Deliver an event to specific users, wherever they are connected.
inline constexpr std::string_view kUserBroadcast = "rtc:cluster:user";
// Deliver an event to everyone in a conversation room.
inline constexpr std::string_view kRoomBroadcast = "rtc:cluster:room";
// Presence transitions, so every instance has a coherent online view.
inline constexpr std::string_view kPresence = "rtc:cluster:presence";
// Notification fan-out to a user's connections on other instances.
inline constexpr std::string_view kNotification = "rtc:cluster:notification";
}  // namespace cluster_channels

// Cross-instance message bus for horizontal scaling.
//
// The problem it solves: WebSocket connections are pinned to whichever instance
// accepted them. When instance A persists a message, the recipients may be
// connected to instances B and C, which know nothing about it. A single-instance
// deployment never notices; the moment a second replica exists, delivery silently
// becomes partial.
//
// The fix is a publish/subscribe hop: after delivering to its own connections, an
// instance publishes the same event on a shared channel, and every other instance
// delivers it to *its* connections. Redis Pub/Sub is the right primitive here —
// fire-and-forget, at-most-once, low latency. Durability is explicitly not wanted:
// a real-time frame that arrives late is worse than useless, and message
// durability is already provided by PostgreSQL, which is the source of truth a
// reconnecting client re-reads from.
//
// Loop suppression: every message carries the publishing instance's node id, and
// a receiver drops messages bearing its own id. Without that, an instance would
// deliver every event twice — once locally, once from its own broadcast.
//
// Implementations must be thread-safe. Handlers are invoked on the bus's own
// subscriber thread, never on a request thread.
class IClusterBus {
  public:
    // Receives a decoded message body. `origin_node` is provided so a handler can
    // reason about provenance; self-originated messages are already filtered out
    // before the handler runs.
    using Handler = std::function<void(std::string_view origin_node, const nlohmann::json& body)>;

    virtual ~IClusterBus() = default;

    // Publishes on `channel`. Never throws — a cluster hop failing must degrade
    // delivery, not fail the user action that triggered it.
    virtual void publish(std::string_view channel, const nlohmann::json& body) noexcept = 0;

    // Registers a handler. Call during bootstrap, before start().
    virtual void subscribe(std::string_view channel, Handler handler) = 0;

    // Begins receiving. Idempotent.
    virtual void start() = 0;

    // Stops receiving and joins the subscriber thread. Idempotent.
    virtual void stop() = 0;

    // This instance's identity, stamped on every outbound message.
    [[nodiscard]] virtual std::string_view node_id() const noexcept = 0;

    // False for the no-op bus. Lets the health endpoint report whether the
    // deployment is actually capable of running more than one replica.
    [[nodiscard]] virtual bool is_distributed() const noexcept = 0;

    [[nodiscard]] virtual std::uint64_t published_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t received_count() const noexcept = 0;
};

// No-op bus for single-instance deployments and tests.
//
// Correct — not merely convenient — when there is one replica: the local delivery
// path already reaches every connection, so the extra hop would be pure cost.
// Selecting this is what makes Redis optional rather than required.
class NullClusterBus final : public IClusterBus {
  public:
    explicit NullClusterBus(std::string node_id) noexcept : node_id_(std::move(node_id)) {}

    void publish(std::string_view /*channel*/, const nlohmann::json& /*body*/) noexcept override {}
    void subscribe(std::string_view /*channel*/, Handler /*handler*/) override {}
    void start() override {}
    void stop() override {}

    [[nodiscard]] std::string_view node_id() const noexcept override { return node_id_; }
    [[nodiscard]] bool is_distributed() const noexcept override { return false; }
    [[nodiscard]] std::uint64_t published_count() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t received_count() const noexcept override { return 0; }

  private:
    std::string node_id_;
};

// Generates a stable-per-process node identity. Prefers the RTC_NODE_ID
// environment variable (Kubernetes sets it from the pod name — see
// deploy/k8s/deployment.yaml), falling back to a random token.
[[nodiscard]] std::string make_node_id();

}  // namespace rtc::realtime
