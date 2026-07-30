#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace rtc::jobs {

// Runs a set of named maintenance tasks on a fixed interval from a dedicated
// thread (expired-session cleanup, cache purge, metric snapshots). Tasks are
// registered before start(); each tick runs them all, isolating and logging any
// exception. The sleep is interruptible so shutdown is immediate.
class PeriodicScheduler {
public:
    using Task = std::function<void()>;

    explicit PeriodicScheduler(std::chrono::seconds interval) : interval_(interval) {}
    ~PeriodicScheduler();

    PeriodicScheduler(const PeriodicScheduler&) = delete;
    PeriodicScheduler& operator=(const PeriodicScheduler&) = delete;

    // Registers a task. Call before start().
    void add(std::string name, Task task) {
        tasks_.emplace_back(std::move(name), std::move(task));
    }

    void start();
    void stop();

    // True between start() and stop(). Read by /health/ready: an instance whose
    // maintenance thread has died still serves requests but silently stops
    // expiring sessions and purging the cache, so it should be taken out of
    // rotation rather than left quietly degrading.
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

    [[nodiscard]] std::size_t task_count() const noexcept { return tasks_.size(); }

private:
    void run();

    std::chrono::seconds interval_;
    std::vector<std::pair<std::string, Task>> tasks_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
};

}  // namespace rtc::jobs
