#include "rtc/jobs/background_executor.hpp"

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "rtc/logging/log_context.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::jobs {

BackgroundExecutor::BackgroundExecutor(std::size_t worker_count)
    : worker_count_(worker_count == 0 ? 1 : worker_count) {}

BackgroundExecutor::~BackgroundExecutor() {
    stop();
}

void BackgroundExecutor::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    workers_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
    RTC_LOG_INFO("Background executor started with {} worker(s)", worker_count_);
}

void BackgroundExecutor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    RTC_LOG_INFO(
        "Background executor stopped ({} completed, {} failed)", completed_.load(), failed_.load());
}

void BackgroundExecutor::submit(Task task) {
    // Correlation is captured here, on the submitting thread, and restored on
    // the worker.
    //
    // Both the trace context and the request id are thread-local, and a queued
    // task runs on a different thread — so without this hop, work deferred off
    // the request path (push notifications, fan-out) produces logs with no
    // request id and spans in an unrelated trace. The effect is that precisely
    // the work an operator cannot watch directly is also the work they cannot
    // correlate.
    //
    // Capturing at submit time rather than asking call sites to pass a context
    // means every existing submit() gains propagation without being touched.
    std::optional<tracing::SpanContext> parent;
    if (const auto* context = tracing::current_span_context();
        context != nullptr && context->is_valid()) {
        parent = *context;
    }
    std::string request_id{logging::current_request_id()};

    Task carried = [inner = std::move(task),
                    parent = std::move(parent),
                    request_id = std::move(request_id)]() mutable {
        // Scoped, so the worker thread is left exactly as it was found. A
        // thread pool reuses its threads: leaking either value would attach it
        // to whatever unrelated task ran next, mislabelling it in the most
        // convincing way possible.
        std::optional<tracing::SpanScope> span_scope;
        if (parent) {
            span_scope.emplace(*parent);
        }
        const logging::RequestIdScope id_scope(std::move(request_id));
        inner();
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load()) {
            return;
        }
        tasks_.push(std::move(carried));
    }
    cv_.notify_one();
}

std::size_t BackgroundExecutor::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void BackgroundExecutor::worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_.load() || !tasks_.empty(); });
            if (!running_.load() && tasks_.empty()) {
                return;  // drained and stopping
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
            completed_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& ex) {
            failed_.fetch_add(1, std::memory_order_relaxed);
            RTC_LOG_ERROR("Background task threw: {}", ex.what());
        } catch (...) {
            failed_.fetch_add(1, std::memory_order_relaxed);
            RTC_LOG_ERROR("Background task threw a non-standard exception");
        }
    }
}

}  // namespace rtc::jobs
