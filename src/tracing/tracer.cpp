#include "rtc/tracing/tracer.hpp"

#include <algorithm>
#include <charconv>
#include <exception>
#include <utility>

#include "rtc/logging/logger.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::tracing {
namespace {

// Deterministic trace-id-ratio sampling, matching the OpenTelemetry spec: take
// the low 8 bytes of the trace id as a big-endian unsigned integer and keep the
// trace when it falls below ratio * 2^64. Because the decision is a pure
// function of the trace id, every service in a distributed trace independently
// reaches the same verdict — no coordination required.
[[nodiscard]] bool trace_id_ratio_sampled(std::string_view trace_id, double ratio) noexcept {
    if (ratio >= 1.0)
        return true;
    if (ratio <= 0.0)
        return false;
    if (trace_id.size() < kTraceIdHexLength)
        return false;

    std::uint64_t low = 0;
    const std::string_view tail = trace_id.substr(kTraceIdHexLength - 16);
    const char* begin = tail.data();
    const char* end = begin + tail.size();
    if (const auto [ptr, ec] = std::from_chars(begin, end, low, 16);
        ec != std::errc{} || ptr != end) {
        return false;
    }
    // Compare in double space; 2^64 is exactly representable, and the precision
    // loss on `low` is irrelevant for a sampling threshold.
    return static_cast<double>(low) < ratio * 18446744073709551616.0;
}

// The fallback tracer returned by tracer() before (or after) one is installed.
// Disabled, so every span it hands out is inert.
Tracer& disabled_tracer() noexcept {
    static Tracer instance(
        Resource{}, std::make_unique<NullSpanExporter>(), TracerOptions{.enabled = false});
    return instance;
}

std::atomic<Tracer*> g_tracer{nullptr};

}  // namespace

void set_tracer(Tracer* tracer) noexcept {
    g_tracer.store(tracer, std::memory_order_release);
}

Tracer& tracer() noexcept {
    Tracer* installed = g_tracer.load(std::memory_order_acquire);
    return installed != nullptr ? *installed : disabled_tracer();
}

Tracer::Tracer(Resource resource, std::unique_ptr<ISpanExporter> exporter, TracerOptions options)
    : resource_(std::move(resource)), exporter_(std::move(exporter)), options_(options) {
    if (exporter_ == nullptr) {
        exporter_ = std::make_unique<NullSpanExporter>();
    }
    options_.sample_ratio = std::clamp(options_.sample_ratio, 0.0, 1.0);
    options_.max_batch_size = std::max<std::size_t>(options_.max_batch_size, 1);
    options_.max_queue_size = std::max(options_.max_queue_size, options_.max_batch_size);
}

Tracer::~Tracer() {
    stop();
}

void Tracer::start() {
    if (!options_.enabled || running_.exchange(true)) {
        return;
    }
    buffer_.reserve(options_.max_batch_size);
    worker_ = std::thread([this] { exporter_loop(); });
    RTC_LOG_INFO("Tracing enabled: exporter={} service={} sample_ratio={}",
                 exporter_->name(),
                 resource_.service_name,
                 options_.sample_ratio);
}

void Tracer::stop() {
    // Join the exporter thread only if it was ever started...
    if (running_.exchange(false)) {
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    // ...but drain unconditionally. Spans can be buffered without the thread ever
    // running: an enabled tracer accepts submissions from the moment it is
    // constructed, and start() may not have been reached (a failure during
    // bootstrap) or may never be called at all. Returning early in that case
    // silently discarded them. drain_once() on an empty buffer is a no-op, so this
    // is also safe on the repeat call from the destructor.
    drain_once();
    exporter_->flush();
}

bool Tracer::should_sample(std::string_view trace_id) const noexcept {
    return trace_id_ratio_sampled(trace_id, options_.sample_ratio);
}

Span Tracer::start_span(std::string name, SpanKind kind, const SpanContext* parent) {
    if (!options_.enabled) {
        return Span{};
    }

    SpanData data;
    data.name = std::move(name);
    data.kind = kind;
    data.span_id = new_span_id();
    data.start_time = std::chrono::system_clock::now();

    if (parent != nullptr && parent->is_valid()) {
        // Parent-based: inherit the trace and the upstream sampling decision so
        // a trace is never captured in fragments.
        data.trace_id = parent->trace_id;
        data.parent_span_id = parent->span_id;
        data.trace_state = parent->trace_state;
        data.sampled = parent->sampled;
    } else {
        data.trace_id = new_trace_id();
        data.sampled = should_sample(data.trace_id);
    }

    started_.fetch_add(1, std::memory_order_relaxed);
    if (!data.sampled) {
        // Unsampled: hand back an inert span. Attribute setters become no-ops and
        // nothing is buffered, so an unsampled request pays almost nothing —
        // while the *context* still propagates via the middleware's response
        // header, keeping the downstream decision consistent.
        return Span{};
    }
    return Span(*this, std::move(data));
}

Span Tracer::start_child_span(std::string name, SpanKind kind) {
    return start_span(std::move(name), kind, current_span_context());
}

void Tracer::submit(SpanData span) noexcept {
    try {
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (buffer_.size() >= options_.max_queue_size) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;  // shed load rather than grow without bound
            }
            buffer_.push_back(std::move(span));
            should_notify = buffer_.size() >= options_.max_batch_size;
        }
        if (should_notify) {
            cv_.notify_one();
        }
    } catch (...) {
        // Instrumentation must never propagate a failure into the traced path.
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Tracer::drain_once() {
    std::vector<SpanData> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.empty()) {
            return;
        }
        batch.swap(buffer_);
        buffer_.reserve(options_.max_batch_size);
    }
    try {
        exporter_->export_spans(resource_, batch);
        exported_.fetch_add(batch.size(), std::memory_order_relaxed);
    } catch (const std::exception& ex) {
        dropped_.fetch_add(batch.size(), std::memory_order_relaxed);
        RTC_LOG_WARN("Span export failed via {}: {}", exporter_->name(), ex.what());
    } catch (...) {
        dropped_.fetch_add(batch.size(), std::memory_order_relaxed);
        RTC_LOG_WARN("Span export failed via {}: unknown error", exporter_->name());
    }
}

void Tracer::exporter_loop() {
    while (running_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Wake on a full batch, on the schedule delay, or on shutdown.
            cv_.wait_for(lock, options_.schedule_delay, [this] {
                return !running_.load(std::memory_order_acquire) ||
                       buffer_.size() >= options_.max_batch_size;
            });
        }
        drain_once();
    }
}

}  // namespace rtc::tracing
