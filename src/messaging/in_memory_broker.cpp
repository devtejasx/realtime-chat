#include "rtc/messaging/in_memory_broker.hpp"

#include <algorithm>
#include <utility>

#include "rtc/logging/logger.hpp"

namespace rtc::messaging {

InMemoryMessageBroker::InMemoryMessageBroker(Options options) : options_(std::move(options)) {}

InMemoryMessageBroker::~InMemoryMessageBroker() {
    stop();
}

bool InMemoryMessageBroker::publish(const Message& message) noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                // Refusing after stop() is deliberate. Accepting a message that
                // will never be consumed reports success for work nobody will
                // do, which is the failure mode this whole interface exists to
                // avoid.
                return false;
            }
            Message queued = message;
            queued.delivery_tag = std::to_string(next_tag_++);
            queued.delivery_attempt = 0;
            queue_.push_back(std::move(queued));
        }
        published_.fetch_add(1, std::memory_order_relaxed);
        queued_.notify_one();
        return true;
    } catch (...) {
        return false;
    }
}

void InMemoryMessageBroker::subscribe(std::string_view topic, Consumer consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    consumers_[std::string(topic)] = std::move(consumer);
}

void InMemoryMessageBroker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;
    workers_.reserve(options_.worker_count);
    for (std::size_t i = 0; i < options_.worker_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

void InMemoryMessageBroker::stop() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
        workers.swap(workers_);
    }
    queued_.notify_all();
    // Joined outside the lock: a worker finishing its current handler needs the
    // mutex to record the outcome, and holding it here would deadlock the
    // shutdown against the very message it is waiting for.
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    settled_.notify_all();
}

void InMemoryMessageBroker::worker_loop() {
    while (true) {
        Message message;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queued_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_ && queue_.empty()) {
                return;
            }
            if (queue_.empty()) {
                continue;
            }
            message = std::move(queue_.front());
            queue_.pop_front();
            ++in_flight_;
        }
        dispatch(std::move(message));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --in_flight_;
        }
        settled_.notify_all();
    }
}

void InMemoryMessageBroker::dispatch(Message message) {
    Consumer consumer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = consumers_.find(message.topic);
        if (it != consumers_.end()) {
            consumer = it->second;  // copied so the handler runs without the lock
        }
    }

    if (!consumer) {
        // Nobody is listening. Dead-lettered rather than dropped: a message
        // published to a topic with no consumer is almost always a typo in a
        // routing key, and silently discarding it makes that typo invisible.
        RTC_LOG_WARN("No consumer for topic '{}'; dead-lettering", message.topic);
        std::lock_guard<std::mutex> lock(mutex_);
        dead_letters_.push_back(std::move(message));
        dead_lettered_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    message.delivery_attempt += 1;

    Ack outcome = Ack::kReject;
    try {
        outcome = consumer(message);
    } catch (const std::exception& ex) {
        // A handler that throws is treated as a retryable failure rather than a
        // rejection: an exception is usually a surprise (a dropped connection),
        // and the handler had no chance to say otherwise. A handler that knows
        // a message is unprocessable returns kReject explicitly.
        RTC_LOG_WARN("Consumer for '{}' threw on attempt {}: {}",
                     message.topic,
                     message.delivery_attempt,
                     ex.what());
        outcome = Ack::kRetry;
    }

    switch (outcome) {
        case Ack::kAccept:
            consumed_.fetch_add(1, std::memory_order_relaxed);
            return;

        case Ack::kReject:
            // Straight to the dead-letter queue without consuming the retry
            // budget. This is what stops a poison message from occupying a
            // worker for its full retry ladder when it was never going to
            // succeed.
            RTC_LOG_WARN("Message on '{}' rejected; dead-lettering", message.topic);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                dead_letters_.push_back(std::move(message));
            }
            dead_lettered_.fetch_add(1, std::memory_order_relaxed);
            return;

        case Ack::kRetry:
            if (message.delivery_attempt >=
                static_cast<std::uint32_t>(std::max(1, options_.max_attempts))) {
                RTC_LOG_ERROR("Message on '{}' failed {} times; dead-lettering",
                              message.topic,
                              message.delivery_attempt);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    dead_letters_.push_back(std::move(message));
                }
                dead_lettered_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // Backoff before requeueing. Slept on the worker rather than with a
            // timer so a retry storm is bounded by worker count — the queue
            // cannot spin faster than the pool can process it.
            reliability::sleep_for(
                options_.backoff.delay_for(static_cast<int>(message.delivery_attempt) + 1));
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (running_) {
                    queue_.push_back(std::move(message));
                }
            }
            queued_.notify_one();
            return;
    }
}

std::vector<Message> InMemoryMessageBroker::dead_letters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dead_letters_;
}

bool InMemoryMessageBroker::drain(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return settled_.wait_for(lock, timeout, [this] { return queue_.empty() && in_flight_ == 0; });
}

}  // namespace rtc::messaging
