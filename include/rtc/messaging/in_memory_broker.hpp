#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rtc/messaging/message_broker.hpp"
#include "rtc/reliability/retry_policy.hpp"

namespace rtc::messaging {

// In-process broker implementing the delivery *semantics* without a network.
//
// What it is for
// --------------
// Two things, and it is worth being precise about which:
//
//   1. Local development and tests. A developer running the stack without
//      RabbitMQ still exercises the real publish/consume path, retry ladder and
//      dead-letter behaviour rather than a no-op.
//
//   2. Keeping the hard part testable. Retry counting, backoff, dead-lettering
//      and poison-message handling are where the bugs live, and they are
//      transport-independent. Implementing them here means the RabbitMQ adapter
//      is responsible only for moving bytes — and this logic is covered by tests
//      that need no broker running.
//
// What it is NOT
// --------------
// Durable. Everything lives in this process's memory, so a restart loses the
// queue. is_durable() returns false accordingly, and /health/ready reports it,
// because a deployment that believes it has durability and does not is worse off
// than one that knows it does not.
//
// Thread-safe: publish from request threads, consumers on worker threads.
class InMemoryMessageBroker final : public IMessageBroker {
  public:
    struct Options {
        // Attempts before a message is dead-lettered, including the first.
        int max_attempts = 3;
        // Backoff between attempts. Reuses RetryPolicy so the ladder is the same
        // one used everywhere else rather than a second implementation.
        reliability::RetryPolicy backoff{
            .max_attempts = 3, .initial_delay = std::chrono::milliseconds{50}};
        // Consumer threads.
        std::size_t worker_count = 1;
    };

    // Options is passed explicitly, matching AuthorizationService and
    // JwtTokenService. A defaulted `Options = {}` does not compile: a nested
    // type's default member initialisers are not usable inside the enclosing
    // class definition, so the default argument cannot be formed.
    explicit InMemoryMessageBroker(Options options);
    ~InMemoryMessageBroker() override;

    InMemoryMessageBroker(const InMemoryMessageBroker&) = delete;
    InMemoryMessageBroker& operator=(const InMemoryMessageBroker&) = delete;

    [[nodiscard]] bool publish(const Message& message) noexcept override;
    void subscribe(std::string_view topic, Consumer consumer) override;
    void start() override;
    void stop() override;

    [[nodiscard]] bool is_durable() const noexcept override { return false; }
    [[nodiscard]] std::uint64_t published_count() const noexcept override {
        return published_.load();
    }
    [[nodiscard]] std::uint64_t consumed_count() const noexcept override {
        return consumed_.load();
    }
    [[nodiscard]] std::uint64_t dead_lettered_count() const noexcept override {
        return dead_lettered_.load();
    }

    // Messages that exhausted their attempts or were rejected outright.
    //
    // Exposed rather than merely counted: a dead-letter queue whose contents
    // cannot be read is a bin, and the reason to keep failed messages at all is
    // to be able to look at them.
    [[nodiscard]] std::vector<Message> dead_letters() const;

    // Blocks until the queue is empty and no handler is running, or the timeout
    // elapses. Returns true if it drained. For tests, so they assert on a
    // settled state rather than sleeping and hoping.
    [[nodiscard]] bool drain(std::chrono::milliseconds timeout = std::chrono::seconds{5});

  private:
    void worker_loop();
    void dispatch(Message message);

    const Options options_;

    mutable std::mutex mutex_;
    std::condition_variable queued_;
    std::condition_variable settled_;
    std::deque<Message> queue_;
    std::map<std::string, Consumer, std::less<>> consumers_;
    std::vector<Message> dead_letters_;
    std::vector<std::thread> workers_;
    int in_flight_ = 0;
    bool running_ = false;
    std::uint64_t next_tag_ = 1;

    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> consumed_{0};
    std::atomic<std::uint64_t> dead_lettered_{0};
};

}  // namespace rtc::messaging
