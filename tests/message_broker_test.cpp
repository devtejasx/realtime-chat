#include "rtc/messaging/message_broker.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/messaging/in_memory_broker.hpp"

// Durable-messaging semantics: retry, dead-lettering, poison handling, shutdown.
//
// These are the parts where the bugs live, and they are transport-independent —
// which is why they are implemented once, above the transport, and tested here
// with no broker running. A RabbitMQ adapter is then responsible only for moving
// bytes, and this behaviour cannot silently differ between the two.
//
// The distinction being asserted throughout: Redis Pub/Sub carries realtime
// frames where loss is acceptable, and this carries deferred work where it is
// not. A message that is dropped here does not degrade the experience — it means
// an email nobody sent.

namespace {

using namespace std::chrono_literals;
using rtc::messaging::Ack;
using rtc::messaging::InMemoryMessageBroker;
using rtc::messaging::Message;
using rtc::messaging::NullMessageBroker;

[[nodiscard]] Message make_message(std::string topic, nlohmann::json body = {}) {
    return Message{.topic = std::move(topic), .body = std::move(body)};
}

// Fast backoff so the retry ladder is exercised without the suite waiting on it.
[[nodiscard]] InMemoryMessageBroker::Options fast(int max_attempts = 3) {
    return InMemoryMessageBroker::Options{
        .max_attempts = max_attempts,
        .backoff = {.max_attempts = max_attempts, .initial_delay = 1ms, .jitter_ratio = 0.0},
        .worker_count = 1};
}

// --- null broker -----------------------------------------------------------

TEST(NullBroker, PublishReportsFailureRatherThanPretendingToQueue) {
    // The important property. Returning true would let a caller believe work was
    // queued when nothing will ever run it — surfacing days later as "the emails
    // never arrived", with nothing in the logs to explain why.
    NullMessageBroker broker;
    EXPECT_FALSE(broker.publish(make_message("rtc.email.send")));
    EXPECT_FALSE(broker.is_durable()) << "and it must not claim durability";
}

TEST(NullBroker, LifecycleIsSafeToDriveWithNoBrokerConfigured) {
    NullMessageBroker broker;
    EXPECT_NO_THROW(
        broker.subscribe("rtc.email.send", [](const Message&) { return Ack::kAccept; }));
    EXPECT_NO_THROW(broker.start());
    EXPECT_NO_THROW(broker.stop());
}

// --- delivery --------------------------------------------------------------

TEST(MessageBroker, DeliversToTheSubscriberOfItsTopic) {
    InMemoryMessageBroker broker(fast());
    std::atomic<int> received{0};
    nlohmann::json seen;

    broker.subscribe("rtc.email.send", [&](const Message& m) {
        seen = m.body;
        ++received;
        return Ack::kAccept;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"to", "ada@example.com"}})));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(received.load(), 1);
    EXPECT_EQ(seen.at("to"), "ada@example.com");
    EXPECT_EQ(broker.consumed_count(), 1U);
    EXPECT_EQ(broker.dead_lettered_count(), 0U);
}

TEST(MessageBroker, MessagesForOtherTopicsAreNotDelivered) {
    InMemoryMessageBroker broker(fast());
    std::atomic<int> email{0};
    broker.subscribe("rtc.email.send", [&](const Message&) {
        ++email;
        return Ack::kAccept;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.push.send")));
    ASSERT_TRUE(broker.drain());
    broker.stop();
    EXPECT_EQ(email.load(), 0);
}

TEST(MessageBroker, PublishingToATopicWithNoConsumerDeadLetters) {
    // Almost always a typo in a routing key. Discarding it silently makes the
    // typo invisible — the message publishes successfully and is consumed by
    // nobody, which looks exactly like success.
    InMemoryMessageBroker broker(fast());
    broker.start();
    ASSERT_TRUE(broker.publish(make_message("rtc.typo.sned")));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(broker.dead_lettered_count(), 1U);
    ASSERT_EQ(broker.dead_letters().size(), 1U);
    EXPECT_EQ(broker.dead_letters().front().topic, "rtc.typo.sned");
}

// --- retry -----------------------------------------------------------------

TEST(MessageBroker, RetriesUntilTheHandlerAccepts) {
    InMemoryMessageBroker broker(fast(5));
    std::atomic<int> attempts{0};

    broker.subscribe("rtc.email.send", [&](const Message& m) {
        EXPECT_EQ(m.delivery_attempt, static_cast<std::uint32_t>(attempts.load() + 1))
            << "the handler must be told which attempt this is";
        return ++attempts < 3 ? Ack::kRetry : Ack::kAccept;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send")));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(attempts.load(), 3);
    EXPECT_EQ(broker.consumed_count(), 1U);
    EXPECT_EQ(broker.dead_lettered_count(), 0U);
}

TEST(MessageBroker, DeadLettersAfterExhaustingAttempts) {
    InMemoryMessageBroker broker(fast(3));
    std::atomic<int> attempts{0};
    broker.subscribe("rtc.email.send", [&](const Message&) {
        ++attempts;
        return Ack::kRetry;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send")));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(attempts.load(), 3) << "bounded: a failing message must not retry forever";
    EXPECT_EQ(broker.dead_lettered_count(), 1U);
    EXPECT_EQ(broker.consumed_count(), 0U);
}

TEST(MessageBroker, AThrowingHandlerIsTreatedAsRetryable) {
    // An exception is usually a surprise — a dropped connection — and the
    // handler had no chance to say otherwise. A handler that *knows* a message
    // is unprocessable returns kReject explicitly.
    InMemoryMessageBroker broker(fast(2));
    std::atomic<int> attempts{0};
    broker.subscribe("rtc.email.send", [&](const Message&) -> Ack {
        ++attempts;
        throw std::runtime_error("smtp unreachable");
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send")));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(attempts.load(), 2);
    EXPECT_EQ(broker.dead_lettered_count(), 1U);
}

// --- poison messages -------------------------------------------------------

TEST(MessageBroker, RejectSkipsTheRetryLadderEntirely) {
    // The property that keeps a poison message from occupying a worker for its
    // full retry ladder when it was never going to succeed. Without a distinct
    // reject, "cannot handle" and "could not handle yet" are indistinguishable.
    InMemoryMessageBroker broker(fast(10));
    std::atomic<int> attempts{0};
    broker.subscribe("rtc.email.send", [&](const Message&) {
        ++attempts;
        return Ack::kReject;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"malformed", true}})));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(attempts.load(), 1) << "rejected once, not ten times";
    EXPECT_EQ(broker.dead_lettered_count(), 1U);
}

TEST(MessageBroker, APoisonMessageDoesNotBlockTheOnesBehindIt) {
    InMemoryMessageBroker broker(fast(2));
    std::atomic<int> good{0};
    broker.subscribe("rtc.email.send", [&](const Message& m) {
        if (m.body.value("poison", false)) {
            return Ack::kReject;
        }
        ++good;
        return Ack::kAccept;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"poison", true}})));
    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"poison", false}})));
    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"poison", false}})));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    EXPECT_EQ(good.load(), 2) << "healthy messages must still be delivered";
    EXPECT_EQ(broker.dead_lettered_count(), 1U);
}

// --- dead-letter inspection ------------------------------------------------

TEST(MessageBroker, DeadLetteredMessagesKeepTheirPayloadAndAttemptCount) {
    // A dead-letter queue whose contents cannot be read is a bin. The reason to
    // keep failed messages is to be able to look at them.
    InMemoryMessageBroker broker(fast(2));
    broker.subscribe("rtc.email.send", [](const Message&) { return Ack::kRetry; });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send", {{"to", "bob@example.com"}})));
    ASSERT_TRUE(broker.drain());
    broker.stop();

    const auto letters = broker.dead_letters();
    ASSERT_EQ(letters.size(), 1U);
    EXPECT_EQ(letters.front().body.at("to"), "bob@example.com") << "payload must survive";
    EXPECT_EQ(letters.front().delivery_attempt, 2U) << "and record how hard it was tried";
}

// --- shutdown --------------------------------------------------------------

TEST(MessageBroker, StopWaitsForInFlightHandlers) {
    // A handler interrupted mid-flight would be redelivered on a real broker and
    // lost on this one. Either way the shutdown must not race it.
    InMemoryMessageBroker broker(fast());
    std::atomic<bool> finished{false};
    broker.subscribe("rtc.email.send", [&](const Message&) {
        std::this_thread::sleep_for(80ms);
        finished = true;
        return Ack::kAccept;
    });
    broker.start();

    ASSERT_TRUE(broker.publish(make_message("rtc.email.send")));
    std::this_thread::sleep_for(20ms);  // let the worker pick it up
    broker.stop();

    EXPECT_TRUE(finished.load()) << "stop() returned while a handler was still running";
    EXPECT_EQ(broker.consumed_count(), 1U);
}

TEST(MessageBroker, PublishAfterStopIsRefused) {
    // Accepting a message that will never be consumed reports success for work
    // nobody will do.
    InMemoryMessageBroker broker(fast());
    broker.subscribe("rtc.email.send", [](const Message&) { return Ack::kAccept; });
    broker.start();
    broker.stop();
    EXPECT_FALSE(broker.publish(make_message("rtc.email.send")));
}

TEST(MessageBroker, StartAndStopAreIdempotent) {
    InMemoryMessageBroker broker(fast());
    broker.start();
    EXPECT_NO_THROW(broker.start());
    broker.stop();
    EXPECT_NO_THROW(broker.stop());
}

TEST(MessageBroker, InMemoryDoesNotClaimDurability) {
    // Everything lives in this process. A deployment that believes it has
    // durability and does not is worse off than one that knows it does not.
    InMemoryMessageBroker broker(fast());
    EXPECT_FALSE(broker.is_durable());
}

// --- concurrency -----------------------------------------------------------

TEST(MessageBroker, ConcurrentWorkersDeliverEveryMessageExactlyOnce) {
    InMemoryMessageBroker broker(
        InMemoryMessageBroker::Options{.max_attempts = 3,
                                       .backoff = {.initial_delay = 1ms, .jitter_ratio = 0.0},
                                       .worker_count = 4});
    std::atomic<int> handled{0};
    broker.subscribe("rtc.analytics.event", [&](const Message&) {
        ++handled;
        return Ack::kAccept;
    });
    broker.start();

    constexpr int kCount = 200;
    for (int i = 0; i < kCount; ++i) {
        ASSERT_TRUE(broker.publish(make_message("rtc.analytics.event", {{"i", i}})));
    }
    ASSERT_TRUE(broker.drain(10s));
    broker.stop();

    EXPECT_EQ(handled.load(), kCount) << "a message was dropped or delivered twice";
    EXPECT_EQ(broker.consumed_count(), static_cast<std::uint64_t>(kCount));
    EXPECT_EQ(broker.dead_lettered_count(), 0U);
}

}  // namespace
