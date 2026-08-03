#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "rtc/jobs/background_executor.hpp"
#include "rtc/logging/log_context.hpp"
#include "rtc/realtime/cluster_trace.hpp"
#include "rtc/tracing/scoped_span.hpp"
#include "rtc/tracing/tracer.hpp"

// Trace propagation across thread and process boundaries.
//
// A trace follows a request through one process because the active span context
// is thread-local, and Crow handles a request on one thread. Two boundaries
// break that assumption, and both were unhandled:
//
//   - the cluster bus, where a publish happens on a request thread in one
//     process and delivery happens on a subscriber thread in another;
//   - the background executor, where a queued task runs on a pool thread.
//
// The result was not a degraded trace but a misleading one. A trace that stops
// at the publish looks exactly like a message that was never published — and
// cross-instance delivery is the first thing anyone investigates when a
// recipient reports not receiving something.

namespace {

using nlohmann::json;
using rtc::realtime::cluster_trace::RemoteScope;

// Installs a sampling tracer for the duration of a test.
class TracerFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        tracer_ = std::make_unique<rtc::tracing::Tracer>(
            rtc::tracing::Resource{.service_name = "test"},
            nullptr,
            rtc::tracing::TracerOptions{.enabled = true, .sample_ratio = 1.0});
        rtc::tracing::set_tracer(tracer_.get());
    }
    void TearDown() override { rtc::tracing::set_tracer(nullptr); }

    std::unique_ptr<rtc::tracing::Tracer> tracer_;
};

// --- cluster bus envelope --------------------------------------------------

TEST_F(TracerFixture, StampCarriesTheActiveTraceOntoTheEnvelope) {
    json envelope{{"event", "message.new"}};
    std::string trace_id;
    {
        auto scope = rtc::tracing::trace_scope("publisher");
        trace_id = rtc::tracing::current_span_context()->trace_id;
        rtc::realtime::cluster_trace::stamp(envelope);
    }

    ASSERT_TRUE(envelope.contains("_traceparent")) << envelope.dump();
    const auto extracted = rtc::realtime::cluster_trace::extract(envelope);
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->trace_id, trace_id);
    // The payload must survive untouched.
    EXPECT_EQ(envelope.at("event"), "message.new");
}

TEST_F(TracerFixture, ReceivingRestoresThePublishersTraceOnThisThread) {
    json envelope{{"event", "message.new"}};
    std::string published_trace;
    {
        auto scope = rtc::tracing::trace_scope("publisher");
        published_trace = rtc::tracing::current_span_context()->trace_id;
        rtc::realtime::cluster_trace::stamp(envelope);
    }

    // Nothing active now — as on a subscriber thread that has just woken up.
    ASSERT_EQ(rtc::tracing::current_span_context(), nullptr);
    {
        const RemoteScope remote(envelope);
        ASSERT_TRUE(remote.active());
        const auto* restored = rtc::tracing::current_span_context();
        ASSERT_NE(restored, nullptr) << "handler would run in no trace at all";
        EXPECT_EQ(restored->trace_id, published_trace)
            << "receiver started a fresh trace; the hop is invisible end to end";
    }
    // Restored, so the subscriber thread is left as it was found.
    EXPECT_EQ(rtc::tracing::current_span_context(), nullptr);
}

TEST(TracePropagation, StampIsANoOpWhenNothingIsBeingTraced) {
    // Tracing disabled, or a sampling decision dropped this request. Emitting a
    // field here would only ever decode to an invalid context.
    json envelope{{"event", "x"}};
    rtc::realtime::cluster_trace::stamp(envelope);
    EXPECT_FALSE(envelope.contains("_traceparent"));
    EXPECT_EQ(envelope.size(), 1U);
}

TEST(TracePropagation, MalformedOrAbsentTraceparentIsIgnored) {
    // A peer on a different build must not be able to throw on the subscriber
    // thread, where an exception takes down delivery for every channel.
    for (const auto& envelope : {json::object(),
                                 json{{"_traceparent", 42}},
                                 json{{"_traceparent", ""}},
                                 json{{"_traceparent", "not-a-traceparent"}},
                                 json{{"_traceparent", "00-tooshort-0000-01"}}}) {
        EXPECT_NO_THROW({
            const RemoteScope remote(envelope);
            EXPECT_FALSE(remote.active());
        }) << envelope.dump();
    }
}

TEST_F(TracerFixture, AnUnstampedMessageLeavesTheReceiverUntraced) {
    // What an older publisher sends during a rolling upgrade. It must start a
    // fresh trace rather than inherit whatever the thread last handled.
    const json envelope{{"event", "message.new"}};
    const RemoteScope remote(envelope);
    EXPECT_FALSE(remote.active());
}

// --- background executor ---------------------------------------------------

TEST_F(TracerFixture, TraceAndRequestIdReachTheWorkerThread) {
    rtc::jobs::BackgroundExecutor executor(1);
    executor.start();

    std::string submitted_trace;
    std::atomic<bool> ran{false};
    std::string observed_trace;
    std::string observed_request;

    {
        auto scope = rtc::tracing::trace_scope("request");
        submitted_trace = rtc::tracing::current_span_context()->trace_id;
        const rtc::logging::RequestIdScope id_scope("req-worker");

        executor.submit([&] {
            if (const auto* context = rtc::tracing::current_span_context()) {
                observed_trace = context->trace_id;
            }
            observed_request = std::string(rtc::logging::current_request_id());
            ran = true;
        });
    }

    for (int i = 0; i < 200 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    executor.stop();

    ASSERT_TRUE(ran.load()) << "task never ran";
    EXPECT_EQ(observed_trace, submitted_trace)
        << "deferred work landed in a different trace than the request that queued it";
    EXPECT_EQ(observed_request, "req-worker") << "logs from deferred work carry no request id";
}

TEST_F(TracerFixture, WorkerThreadsAreLeftCleanBetweenTasks) {
    // The pool reuses threads. Leaking either value would attach it to whatever
    // unrelated task ran next — mislabelling it in the most convincing way
    // possible, since the output looks entirely correct.
    rtc::jobs::BackgroundExecutor executor(1);
    executor.start();

    std::atomic<bool> first_done{false};
    {
        auto scope = rtc::tracing::trace_scope("request");
        const rtc::logging::RequestIdScope id_scope("req-first");
        executor.submit([&] { first_done = true; });
    }
    for (int i = 0; i < 200 && !first_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(first_done.load());

    std::atomic<bool> second_done{false};
    bool leaked_trace = true;
    std::string leaked_request = "unset";
    executor.submit([&] {
        leaked_trace = rtc::tracing::current_span_context() != nullptr;
        leaked_request = std::string(rtc::logging::current_request_id());
        second_done = true;
    });
    for (int i = 0; i < 200 && !second_done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    executor.stop();

    ASSERT_TRUE(second_done.load());
    EXPECT_FALSE(leaked_trace) << "previous task's trace leaked onto a reused worker thread";
    EXPECT_TRUE(leaked_request.empty()) << "previous task's request id leaked: " << leaked_request;
}

TEST(TracePropagation, SubmittingWithoutAnActiveTraceStillRuns) {
    // The common case for scheduled work: no request, no trace, must still work.
    rtc::jobs::BackgroundExecutor executor(1);
    executor.start();

    std::atomic<bool> ran{false};
    executor.submit([&] { ran = true; });
    for (int i = 0; i < 200 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    executor.stop();
    EXPECT_TRUE(ran.load());
}

}  // namespace
