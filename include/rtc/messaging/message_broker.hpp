#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace rtc::messaging {

// Durable asynchronous messaging.
//
// Why this exists alongside Redis Pub/Sub
// ---------------------------------------
// The two are not alternatives and neither replaces the other. Redis Pub/Sub is
// fire-and-forget and at-most-once: a subscriber that is not connected when a
// message is published never sees it. That is exactly right for realtime frames
// — a typing indicator or presence change that arrives late is worse than
// useless, and PostgreSQL is already the durable record a reconnecting client
// re-reads from.
//
// It is exactly wrong for work that must eventually happen. A push
// notification, an audit write or a thumbnail render queued in the in-process
// BackgroundExecutor is lost if the pod is terminated before it runs — and
// under Kubernetes, pods are terminated routinely. This interface is for that
// second category: work whose *completion* matters more than its latency.
//
//   Redis Pub/Sub   realtime fan-out   at-most-once   loss is acceptable
//   IMessageBroker  deferred work      at-least-once  loss is not
//
// At-least-once, not exactly-once. A consumer may see the same message twice —
// after a redelivery following a crash between handling and acknowledgement —
// so handlers must be idempotent. Exactly-once delivery is not available over a
// network, and claiming it in an interface would only mislead implementers.
//
// Optional by construction. The composition root selects NullMessageBroker when
// no broker is configured, and the service runs exactly as it does today: this
// adds a durability tier, it does not become a startup dependency.

// A unit of deferred work.
struct Message {
    // Routing key / queue name. Namespaced by the caller, e.g. "rtc.email.send".
    std::string topic;
    // Application payload. JSON rather than an opaque blob so a message parked
    // in a dead-letter queue can be read by a human at 3am without a decoder.
    nlohmann::json body;
    // Redelivery count, set by the broker on receipt. Zero on first delivery.
    // A handler can use it to distinguish "this failed once" from "this has
    // failed twenty times and is never going to work".
    std::uint32_t delivery_attempt = 0;
    // Opaque broker-assigned id, for correlating a delivery with its ack.
    std::string delivery_tag;
};

// What a consumer decided about a message. Returned rather than thrown so the
// three outcomes are explicit at the call site — a handler that merely returns
// void forces the framework to guess what an exception meant.
enum class Ack {
    // Handled. Remove from the queue.
    kAccept,
    // Failed, but plausibly transient. Requeue for another attempt, subject to
    // the broker's retry limit.
    kRetry,
    // Failed and will fail again — malformed payload, deleted entity. Send
    // straight to the dead-letter queue without consuming retry attempts.
    //
    // This is the outcome that keeps a poison message from blocking a queue
    // forever: without it, "cannot handle" and "could not handle *yet*" are
    // indistinguishable, and the broker redelivers a message nobody can ever
    // process until it exhausts a retry budget it should never have entered.
    kReject,
};

using Consumer = std::function<Ack(const Message&)>;

// Publish/consume over a durable broker.
//
// Implementations must be thread-safe: publish is called from request threads,
// and consumers run on the broker's own threads.
class IMessageBroker {
  public:
    virtual ~IMessageBroker() = default;

    // Enqueues durably. Returns false when the message could not be accepted —
    // broker unreachable, publisher confirm not received — so the caller can
    // decide whether to fall back or fail.
    //
    // Deliberately not noexcept-and-silent like the cluster bus. A dropped
    // realtime frame is invisible and acceptable; a dropped email is neither,
    // and a caller that cannot tell the difference cannot compensate.
    [[nodiscard]] virtual bool publish(const Message& message) noexcept = 0;

    // Registers a handler for `topic`. Call during bootstrap, before start().
    virtual void subscribe(std::string_view topic, Consumer consumer) = 0;

    // Begins consuming. Idempotent.
    virtual void start() = 0;

    // Stops consuming and waits for in-flight handlers to finish, so a message
    // being processed during shutdown is acknowledged rather than redelivered.
    // Idempotent.
    virtual void stop() = 0;

    // False for the null implementation. Lets /health/ready report whether the
    // deployment actually has a durability tier, rather than implying one.
    [[nodiscard]] virtual bool is_durable() const noexcept = 0;

    [[nodiscard]] virtual std::uint64_t published_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t consumed_count() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t dead_lettered_count() const noexcept = 0;
};

// Topics this service publishes. Constants rather than string literals at call
// sites, because a typo in a routing key produces a message that is published
// successfully and consumed by nobody — a silence that looks like success.
namespace topics {
inline constexpr std::string_view kEmail = "rtc.email.send";
inline constexpr std::string_view kPushNotification = "rtc.push.send";
inline constexpr std::string_view kAuditWrite = "rtc.audit.write";
inline constexpr std::string_view kThumbnail = "rtc.media.thumbnail";
inline constexpr std::string_view kAnalytics = "rtc.analytics.event";
}  // namespace topics

// No-op broker: the correct choice when no durable broker is configured, not
// merely a fallback.
//
// publish() returns false rather than true. Reporting success for a message
// that was discarded would let a caller believe work was queued when nothing
// will ever run it — the failure would surface days later as "the emails never
// arrived", with nothing in the logs. False lets the caller do the work inline,
// log, or degrade knowingly.
class NullMessageBroker final : public IMessageBroker {
  public:
    [[nodiscard]] bool publish(const Message& /*message*/) noexcept override { return false; }
    void subscribe(std::string_view /*topic*/, Consumer /*consumer*/) override {}
    void start() override {}
    void stop() override {}

    [[nodiscard]] bool is_durable() const noexcept override { return false; }
    [[nodiscard]] std::uint64_t published_count() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t consumed_count() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t dead_lettered_count() const noexcept override { return 0; }
};

}  // namespace rtc::messaging
