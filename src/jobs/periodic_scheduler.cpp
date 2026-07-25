#include "rtc/jobs/periodic_scheduler.hpp"

#include <exception>

#include "rtc/logging/logger.hpp"

namespace rtc::jobs {

PeriodicScheduler::~PeriodicScheduler() { stop(); }

void PeriodicScheduler::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
    RTC_LOG_INFO("Periodic scheduler started ({} task(s), every {}s)", tasks_.size(),
                 interval_.count());
}

void PeriodicScheduler::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void PeriodicScheduler::run() {
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        }
        if (!running_.load()) {
            break;
        }
        for (const auto& [name, task] : tasks_) {
            try {
                task();
            } catch (const std::exception& ex) {
                RTC_LOG_ERROR("Maintenance task '{}' failed: {}", name, ex.what());
            }
        }
    }
}

}  // namespace rtc::jobs
