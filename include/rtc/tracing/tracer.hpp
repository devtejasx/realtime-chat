#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rtc/tracing/span.hpp"
#include "rtc/tracing/span_exporter.hpp"
#include "rtc/tracing/trace_context.hpp"

namespace rtc::tracing {

// Tracer configuration. Mirrors the knobs the OpenTelemetry SDK exposes through
// OTEL_* environment variables, so operators already familiar with OTel find
// what they expect (see config::Config and .env.example).
struct TracerOptions {
    bool enabled = false;
    // Head-based sampling probability in [0, 1]. Applied only to *root* spans;
    // a span that inherits a sampled parent is always recorded, so a trace is
    // never half-captured (`parentbased_traceidratio`).
    double sample_ratio = 0.05;
    // Spans buffered before the exporter thread is nudged.
    std::size_t max_batch_size = 128;
    // Hard cap on the buffer. Beyond it spans are dropped and counted rather
    // than growing memory without bound under load — losing telemetry is always
    // preferable to destabilising the service it observes.
    std::size_t max_queue_size = 2048;
    std::chrono::milliseconds schedule_delay{5000};
};

// Creates and records spans, then hands finished spans to an exporter off the
// request path.
//
// Threading model: `start_span` and `submit` are lock-free-ish (one short mutex
// acquisition on submit) and callable from any thread. A single owned exporter
// thread drains the buffer on a batch-size or timer trigger. Shutdown drains
// the remainder and joins, so no span is lost on a graceful stop.
//
// The tracer never throws out of `submit` and never propagates exporter errors:
// instrumentation must not be able to fail the operation it measures.
class Tracer {
public:
    Tracer(Resource resource, std::unique_ptr<ISpanExporter> exporter, TracerOptions options);
    ~Tracer();

    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;

    void start();
    void stop();

    // Starts a span parented to `parent` when it is valid, otherwise a root
    // span. Returns an inert Span when tracing is disabled or the sampler
    // declines, which callers can use unconditionally.
    [[nodiscard]] Span start_span(std::string name, SpanKind kind = SpanKind::kInternal,
                                  const SpanContext* parent = nullptr);

    // Starts a span parented to the current thread's active span (see
    // scoped_span.hpp). This is the form nearly all instrumentation uses.
    [[nodiscard]] Span start_child_span(std::string name, SpanKind kind = SpanKind::kInternal);

    // Called by Span's destructor. Never throws.
    void submit(SpanData span) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return options_.enabled; }
    [[nodiscard]] const Resource& resource() const noexcept { return resource_; }

    // Observability of the tracer itself, surfaced on /metrics.
    [[nodiscard]] std::uint64_t spans_started() const noexcept { return started_.load(); }
    [[nodiscard]] std::uint64_t spans_exported() const noexcept { return exported_.load(); }
    [[nodiscard]] std::uint64_t spans_dropped() const noexcept { return dropped_.load(); }

private:
    void exporter_loop();
    void drain_once();
    [[nodiscard]] bool should_sample(std::string_view trace_id) const noexcept;

    Resource resource_;
    std::unique_ptr<ISpanExporter> exporter_;
    TracerOptions options_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<SpanData> buffer_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    std::atomic<std::uint64_t> started_{0};
    std::atomic<std::uint64_t> exported_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

// ---------------------------------------------------------------------------
// Process-wide tracer access.
//
// Tracing is genuinely cross-cutting: the connection pool, the cache store and
// the WebSocket layer all need to emit spans, and threading a `Tracer&` through
// every one of those signatures would be a breaking API change for no design
// benefit. OpenTelemetry itself resolves this with a global provider, and this
// is the one place the project's "no global mutable state" rule is relaxed.
//
// The relaxation is kept as narrow as possible:
//   - the pointer is written exactly twice per process (set at bootstrap, reset
//     at shutdown) and is otherwise read-only;
//   - it is an atomic, so reads are safe and lock-free from any thread;
//   - ownership stays with Application — the provider only borrows;
//   - when unset, tracer() returns a disabled tracer, so nothing branches on it.
// ---------------------------------------------------------------------------

// Installs the process tracer. Pass nullptr to uninstall (during shutdown,
// before the Tracer is destroyed).
void set_tracer(Tracer* tracer) noexcept;

// The installed tracer, or a shared disabled tracer when none is installed.
[[nodiscard]] Tracer& tracer() noexcept;

}  // namespace rtc::tracing
