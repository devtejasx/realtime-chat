#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "rtc/events/domain_event.hpp"
#include "rtc/events/event_dispatcher.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/events/function_subscriber.hpp"
#include "rtc/events/in_process_event_bus.hpp"
#include "rtc/jobs/background_executor.hpp"

namespace {

using rtc::events::DomainEvent;
using rtc::events::EventDispatcher;
using rtc::events::EventType;
using rtc::events::FunctionSubscriber;
using rtc::events::InProcessEventBus;

// Records every event it receives, for a declared set of types.
class RecordingSubscriber final : public rtc::events::IEventSubscriber {
public:
    explicit RecordingSubscriber(std::vector<EventType> types, std::string name = "recorder")
        : name_(std::move(name)) {
        for (const EventType type : types) {
            interest_.set(static_cast<std::size_t>(type));
        }
    }

    [[nodiscard]] bool interested_in(EventType type) const noexcept override {
        return interest_.test(static_cast<std::size_t>(type));
    }
    void handle(const DomainEvent& event) override { received.push_back(event); }
    [[nodiscard]] std::string_view subscriber_name() const noexcept override { return name_; }

    std::vector<DomainEvent> received;

private:
    std::string name_;
    std::bitset<rtc::events::kEventTypeCount> interest_;
};

// Always throws, to prove failures are isolated.
class ThrowingSubscriber final : public rtc::events::IEventSubscriber {
public:
    [[nodiscard]] bool interested_in(EventType) const noexcept override { return true; }
    void handle(const DomainEvent&) override {
        ++calls;
        throw std::runtime_error("subscriber exploded");
    }
    [[nodiscard]] std::string_view subscriber_name() const noexcept override { return "thrower"; }
    int calls = 0;
};

// --- event names and envelope ---------------------------------------------

TEST(DomainEvent, TypeNamesRoundTrip) {
    for (std::size_t i = 0; i < rtc::events::kEventTypeCount; ++i) {
        const auto type = static_cast<EventType>(i);
        const auto parsed = rtc::events::parse_event_type(rtc::events::to_string(type));
        ASSERT_TRUE(parsed.has_value()) << rtc::events::to_string(type);
        EXPECT_EQ(*parsed, type);
    }
}

TEST(DomainEvent, TypeNamesAreUnique) {
    // Duplicate wire names would make audit rows and broker subjects ambiguous.
    for (std::size_t a = 0; a < rtc::events::kEventTypeCount; ++a) {
        for (std::size_t b = a + 1; b < rtc::events::kEventTypeCount; ++b) {
            EXPECT_NE(rtc::events::kEventTypeNames[a], rtc::events::kEventTypeNames[b]);
        }
    }
}

TEST(DomainEvent, UnknownNameIsRejected) {
    EXPECT_FALSE(rtc::events::parse_event_type("nope.nope").has_value());
    EXPECT_FALSE(rtc::events::parse_event_type("").has_value());
}

TEST(DomainEvent, MakeStampsIdentityAndTime) {
    const auto event = DomainEvent::make(EventType::kMessageSent, {{"message_id", 7}}, 42);
    EXPECT_EQ(event.type, EventType::kMessageSent);
    EXPECT_FALSE(event.event_id.empty());
    ASSERT_TRUE(event.actor_id.has_value());
    EXPECT_EQ(*event.actor_id, 42);
    EXPECT_EQ(event.payload["message_id"], 7);
    EXPECT_NE(event.occurred_at.time_since_epoch().count(), 0);
}

TEST(DomainEvent, EventIdsAreDistinct) {
    // The audit table's UNIQUE(event_id) relies on this: colliding ids would make
    // one of two genuinely different events disappear.
    const auto first = DomainEvent::make(EventType::kUserLoggedIn, {}, 1);
    const auto second = DomainEvent::make(EventType::kUserLoggedIn, {}, 1);
    EXPECT_NE(first.event_id, second.event_id);
}

TEST(DomainEvent, SerialisesToJson) {
    auto event = DomainEvent::make(EventType::kMessageDeleted, {{"message_id", 3}}, 9);
    event.correlation_id = "req-1";
    const auto json = event.to_json();
    EXPECT_EQ(json["type"], "message.deleted");
    EXPECT_EQ(json["actor_id"], 9);
    EXPECT_EQ(json["correlation_id"], "req-1");
    EXPECT_EQ(json["payload"]["message_id"], 3);
    EXPECT_TRUE(json.contains("occurred_at"));
}

TEST(DomainEvent, TypedBuildersPopulateTheirPayload) {
    const auto sent = rtc::events::MessageSent{
        .message_id = 11, .conversation_id = 5, .sender_id = 3,
        .recipient_ids = {3, 4}, .content_length = 12}.to_event();
    EXPECT_EQ(sent.type, EventType::kMessageSent);
    EXPECT_EQ(sent.payload["message_id"], 11);
    EXPECT_EQ(sent.payload["content_length"], 12);
    EXPECT_EQ(sent.payload["recipient_ids"].size(), 2U);
    // Content itself must never travel on the bus.
    EXPECT_FALSE(sent.payload.contains("content"));

    const auto banned = rtc::events::AdminAction{
        .actor_id = 1, .action = "user.ban", .target_type = "user", .target_id = "9"}.to_event();
    EXPECT_EQ(banned.type, EventType::kAdminAction);
    EXPECT_EQ(banned.payload["action"], "user.ban");
    EXPECT_EQ(banned.payload["target_id"], "9");
}

// --- dispatcher -----------------------------------------------------------

TEST(EventDispatcher, DeliversOnlyToInterestedSubscribers) {
    EventDispatcher dispatcher;
    RecordingSubscriber wants_sent({EventType::kMessageSent}, "sent");
    RecordingSubscriber wants_deleted({EventType::kMessageDeleted}, "deleted");
    dispatcher.subscribe(wants_sent);
    dispatcher.subscribe(wants_deleted);

    dispatcher.dispatch(DomainEvent::make(EventType::kMessageSent, {}, 1));

    EXPECT_EQ(wants_sent.received.size(), 1U);
    EXPECT_TRUE(wants_deleted.received.empty());
    EXPECT_EQ(dispatcher.dispatched_count(), 1U);
}

TEST(EventDispatcher, IsolatesAFailingSubscriber) {
    // The core guarantee: subscribers are independent side effects, so an audit
    // database outage must not stop notification delivery.
    EventDispatcher dispatcher;
    ThrowingSubscriber thrower;
    RecordingSubscriber recorder({EventType::kMessageSent});
    dispatcher.subscribe(thrower);
    dispatcher.subscribe(recorder);

    EXPECT_NO_THROW(dispatcher.dispatch(DomainEvent::make(EventType::kMessageSent, {}, 1)));

    EXPECT_EQ(thrower.calls, 1);
    EXPECT_EQ(recorder.received.size(), 1U) << "a failing subscriber blocked a later one";
    EXPECT_EQ(dispatcher.handler_failure_count(), 1U);
}

TEST(EventDispatcher, RefusesDuplicateRegistration) {
    // Registering twice would double-apply the side effect — two audit rows for one
    // event.
    EventDispatcher dispatcher;
    RecordingSubscriber recorder({EventType::kUserRegistered});
    dispatcher.subscribe(recorder);
    dispatcher.subscribe(recorder);

    EXPECT_EQ(dispatcher.subscriber_count(), 1U);
    dispatcher.dispatch(DomainEvent::make(EventType::kUserRegistered, {}, 1));
    EXPECT_EQ(recorder.received.size(), 1U);
}

TEST(EventDispatcher, DispatchWithNoSubscribersIsHarmless) {
    EventDispatcher dispatcher;
    EXPECT_NO_THROW(dispatcher.dispatch(DomainEvent::make(EventType::kUserOnline, {}, 1)));
    EXPECT_EQ(dispatcher.dispatched_count(), 1U);
}

// --- in-process bus -------------------------------------------------------

TEST(InProcessEventBus, SynchronousModeDispatchesInline) {
    EventDispatcher dispatcher;
    RecordingSubscriber recorder({EventType::kUserRegistered});
    InProcessEventBus bus(dispatcher);
    bus.subscribe(recorder);

    EXPECT_FALSE(bus.is_asynchronous());
    bus.publish(DomainEvent::make(EventType::kUserRegistered, {}, 1));

    // No waiting: synchronous mode is what makes tests deterministic.
    EXPECT_EQ(recorder.received.size(), 1U);
    EXPECT_EQ(bus.published_count(), 1U);
    EXPECT_EQ(bus.dropped_count(), 0U);
}

TEST(InProcessEventBus, AsynchronousModeDispatchesOnTheWorkerPool) {
    rtc::jobs::BackgroundExecutor executor(2);
    executor.start();

    EventDispatcher dispatcher;
    std::atomic<int> seen{0};
    FunctionSubscriber counter("counter", {EventType::kMessageSent},
                              [&seen](const DomainEvent&) { ++seen; });
    dispatcher.subscribe(counter);

    InProcessEventBus bus(dispatcher, executor);
    EXPECT_TRUE(bus.is_asynchronous());

    constexpr int kEvents = 25;
    for (int i = 0; i < kEvents; ++i) {
        bus.publish(DomainEvent::make(EventType::kMessageSent, {{"n", i}}, 1));
    }

    // Poll rather than sleep a fixed duration: bounded, and fast when it passes.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (seen.load() < kEvents && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    executor.stop();

    EXPECT_EQ(seen.load(), kEvents);
    EXPECT_EQ(bus.published_count(), static_cast<std::uint64_t>(kEvents));
}

TEST(InProcessEventBus, PublishNeverThrows) {
    // IEventPublisher::publish is noexcept: the event has already happened, so a
    // delivery problem must not surface to the producer.
    EventDispatcher dispatcher;
    ThrowingSubscriber thrower;
    dispatcher.subscribe(thrower);
    InProcessEventBus bus(dispatcher);

    EXPECT_NO_THROW(bus.publish(DomainEvent::make(EventType::kMessageSent, {}, 1)));
}

TEST(NullEventPublisher, SwallowsEverything) {
    // The default for a service with no bus injected — this is what keeps the event
    // bus a non-breaking addition.
    auto& publisher = rtc::events::NullEventPublisher::instance();
    EXPECT_NO_THROW(publisher.publish(DomainEvent::make(EventType::kMessageSent, {}, 1)));
}

TEST(FunctionSubscriber, WildcardFormReceivesEveryType) {
    EventDispatcher dispatcher;
    int seen = 0;
    FunctionSubscriber tap("tap", [&seen](const DomainEvent&) { ++seen; });
    dispatcher.subscribe(tap);

    dispatcher.dispatch(DomainEvent::make(EventType::kUserOnline, {}, 1));
    dispatcher.dispatch(DomainEvent::make(EventType::kAdminAction, {}, 1));
    EXPECT_EQ(seen, 2);
}

}  // namespace
