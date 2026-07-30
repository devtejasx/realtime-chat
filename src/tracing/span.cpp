#include "rtc/tracing/span.hpp"

#include <utility>

#include "rtc/tracing/tracer.hpp"

namespace rtc::tracing {

Span::Span(Tracer& tracer, SpanData data) noexcept
    : tracer_(&tracer),
      data_(std::move(data)),
      started_(std::chrono::steady_clock::now()),
      ended_(false) {
    context_.trace_id = data_.trace_id;
    context_.span_id = data_.span_id;
    context_.trace_state = data_.trace_state;
    context_.sampled = data_.sampled;
}

Span::Span(Span&& other) noexcept
    : tracer_(other.tracer_),
      data_(std::move(other.data_)),
      context_(std::move(other.context_)),
      started_(other.started_),
      ended_(other.ended_) {
    // Neutralise the source so only one object can report this span.
    other.tracer_ = nullptr;
    other.ended_ = true;
}

Span& Span::operator=(Span&& other) noexcept {
    if (this != &other) {
        end();  // report whatever this object was holding before overwriting it
        tracer_ = other.tracer_;
        data_ = std::move(other.data_);
        context_ = std::move(other.context_);
        started_ = other.started_;
        ended_ = other.ended_;
        other.tracer_ = nullptr;
        other.ended_ = true;
    }
    return *this;
}

Span::~Span() {
    end();
}

Span& Span::set_attribute(std::string key, std::string value) {
    if (is_recording()) {
        data_.attributes.emplace_back(std::move(key), std::move(value));
    }
    return *this;
}

Span& Span::set_attribute(std::string key, std::int64_t value) {
    return set_attribute(std::move(key), std::to_string(value));
}

Span& Span::set_attribute(std::string key, double value) {
    return set_attribute(std::move(key), std::to_string(value));
}

Span& Span::set_attribute(std::string key, bool value) {
    return set_attribute(std::move(key), std::string(value ? "true" : "false"));
}

Span& Span::set_status(SpanStatus status, std::string message) {
    if (is_recording()) {
        data_.status = status;
        data_.status_message = std::move(message);
    }
    return *this;
}

Span& Span::record_error(std::string_view message) {
    if (is_recording()) {
        data_.status = SpanStatus::kError;
        data_.status_message = std::string(message);
        data_.attributes.emplace_back("exception.message", std::string(message));
    }
    return *this;
}

void Span::end() {
    if (tracer_ == nullptr || ended_) {
        return;
    }
    ended_ = true;
    // Duration comes from the monotonic clock (immune to wall-clock jumps),
    // while start_time stays on the system clock because that is what exporters
    // need for absolute timestamps.
    data_.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_);
    tracer_->submit(std::move(data_));
    tracer_ = nullptr;
}

}  // namespace rtc::tracing
