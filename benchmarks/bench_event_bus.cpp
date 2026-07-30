// Domain event bus.
//
// Publishing happens inside request handlers, so the *publish* cost is what lands
// on user-facing latency; dispatch happens on the worker pool and only affects
// background throughput. Those two are measured separately, because conflating them
// would hide the fact that asynchronous mode exists precisely to move the expensive
// half off the request path.
#include <benchmark/benchmark.h>

#include <atomic>
#include <string>

#include "rtc/events/domain_event.hpp"
#include "rtc/events/event_dispatcher.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/events/function_subscriber.hpp"
#include "rtc/events/in_process_event_bus.hpp"
#include "rtc/jobs/background_executor.hpp"

namespace {

using rtc::events::DomainEvent;
using rtc::events::EventType;

// Envelope construction alone: id generation, timestamp, and adopting the ambient
// trace context. Paid on every publish regardless of delivery mode.
void BM_EventCreate(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::events::MessageSent{
            .message_id = 1,
            .conversation_id = 2,
            .sender_id = 3,
            .recipient_ids = {3, 4, 5},
            .content_length = 42,
        }
                                     .to_event());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_EventCreate);

// Serialisation, as the audit writer and any broker forwarder would do it.
void BM_EventToJson(benchmark::State& state) {
    const auto event = rtc::events::MessageSent{.message_id = 1,
                                                .conversation_id = 2,
                                                .sender_id = 3,
                                                .recipient_ids = {3, 4, 5},
                                                .content_length = 42}
                           .to_event();

    for (auto _ : state) {
        benchmark::DoNotOptimize(event.to_json());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_EventToJson);

// Dispatch with no subscribers: the floor cost of the shared lock plus the
// interest scan.
void BM_DispatchNoSubscribers(benchmark::State& state) {
    rtc::events::EventDispatcher dispatcher;
    const auto event = DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1);

    for (auto _ : state) {
        dispatcher.dispatch(event);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DispatchNoSubscribers);

// Dispatch cost as the subscriber count grows. Interest is a bitset test, so the
// uninterested case should stay essentially free — which is what makes it safe to
// register many narrowly-scoped subscribers.
void BM_DispatchUninterestedSubscribers(benchmark::State& state) {
    rtc::events::EventDispatcher dispatcher;
    std::vector<std::unique_ptr<rtc::events::FunctionSubscriber>> subscribers;
    const auto count = static_cast<std::size_t>(state.range(0));

    for (std::size_t i = 0; i < count; ++i) {
        // Interested in a type that is never published.
        subscribers.push_back(std::make_unique<rtc::events::FunctionSubscriber>(
            "sub-" + std::to_string(i),
            std::initializer_list<EventType>{EventType::kAdminAction},
            [](const DomainEvent&) {}));
        dispatcher.subscribe(*subscribers.back());
    }

    const auto event = DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1);
    for (auto _ : state) {
        dispatcher.dispatch(event);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DispatchUninterestedSubscribers)->Arg(1)->Arg(8)->Arg(64);

void BM_DispatchInterestedSubscribers(benchmark::State& state) {
    rtc::events::EventDispatcher dispatcher;
    std::vector<std::unique_ptr<rtc::events::FunctionSubscriber>> subscribers;
    std::atomic<std::uint64_t> sink{0};
    const auto count = static_cast<std::size_t>(state.range(0));

    for (std::size_t i = 0; i < count; ++i) {
        subscribers.push_back(std::make_unique<rtc::events::FunctionSubscriber>(
            "sub-" + std::to_string(i),
            std::initializer_list<EventType>{EventType::kMessageSent},
            [&sink](const DomainEvent&) { sink.fetch_add(1, std::memory_order_relaxed); }));
        dispatcher.subscribe(*subscribers.back());
    }

    const auto event = DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1);
    for (auto _ : state) {
        dispatcher.dispatch(event);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DispatchInterestedSubscribers)->Arg(1)->Arg(4)->Arg(16);

// Synchronous publish: the request thread pays for every subscriber inline. This is
// the number to compare against the asynchronous case below.
void BM_PublishSynchronous(benchmark::State& state) {
    rtc::events::EventDispatcher dispatcher;
    std::atomic<std::uint64_t> sink{0};
    rtc::events::FunctionSubscriber subscriber(
        "sink", {EventType::kMessageSent}, [&sink](const DomainEvent&) {
            sink.fetch_add(1, std::memory_order_relaxed);
        });
    dispatcher.subscribe(subscriber);

    rtc::events::InProcessEventBus bus(dispatcher);
    for (auto _ : state) {
        bus.publish(DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PublishSynchronous);

// Asynchronous publish: the request thread only enqueues. This measures exactly
// what a handler pays, and should be markedly cheaper than the synchronous case
// once a subscriber does any real work — which is the entire justification for
// dispatching on the worker pool.
//
// Note this deliberately does not wait for drain: the point is the producer-side
// cost, not end-to-end latency.
void BM_PublishAsynchronous(benchmark::State& state) {
    rtc::jobs::BackgroundExecutor executor(2);
    executor.start();

    rtc::events::EventDispatcher dispatcher;
    std::atomic<std::uint64_t> sink{0};
    rtc::events::FunctionSubscriber subscriber(
        "sink", {EventType::kMessageSent}, [&sink](const DomainEvent&) {
            sink.fetch_add(1, std::memory_order_relaxed);
        });
    dispatcher.subscribe(subscriber);

    rtc::events::InProcessEventBus bus(dispatcher, executor);
    for (auto _ : state) {
        bus.publish(DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1));
    }
    state.PauseTiming();
    executor.stop();
    state.ResumeTiming();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PublishAsynchronous);

// The null publisher — what a service uses when no bus is injected. Should be
// indistinguishable from zero, which is what makes the event bus a free addition
// for callers that do not want it.
void BM_PublishNull(benchmark::State& state) {
    auto& publisher = rtc::events::NullEventPublisher::instance();
    for (auto _ : state) {
        publisher.publish(DomainEvent::make(EventType::kMessageSent, {{"id", 1}}, 1));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PublishNull);

// Worker pool submission cost in isolation.
void BM_BackgroundExecutorSubmit(benchmark::State& state) {
    rtc::jobs::BackgroundExecutor executor(4);
    executor.start();
    std::atomic<std::uint64_t> sink{0};

    for (auto _ : state) {
        executor.submit([&sink] { sink.fetch_add(1, std::memory_order_relaxed); });
    }
    state.PauseTiming();
    executor.stop();
    state.ResumeTiming();

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BackgroundExecutorSubmit);

}  // namespace
