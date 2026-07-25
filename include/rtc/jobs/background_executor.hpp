#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace rtc::jobs {

// A fixed-size worker-thread pool for off-request work: thumbnail generation,
// notification delivery, cache cleanup, and other tasks that must not block the
// HTTP/WebSocket threads. Tasks are `std::function<void()>`; exceptions thrown
// by a task are caught and logged so one bad job never takes down a worker.
// Shutdown drains cleanly and joins all workers.
class BackgroundExecutor {
public:
    using Task = std::function<void()>;

    explicit BackgroundExecutor(std::size_t worker_count);
    ~BackgroundExecutor();

    BackgroundExecutor(const BackgroundExecutor&) = delete;
    BackgroundExecutor& operator=(const BackgroundExecutor&) = delete;

    void start();
    void stop();

    // Enqueues a task. Silently drops the task if the pool is stopped, so
    // late submissions during shutdown are safe.
    void submit(Task task);

    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] std::uint64_t completed() const noexcept { return completed_.load(); }
    [[nodiscard]] std::uint64_t failed() const noexcept { return failed_.load(); }
    [[nodiscard]] std::size_t worker_count() const noexcept { return worker_count_; }

private:
    void worker_loop();

    std::size_t worker_count_;
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> completed_{0};
    std::atomic<std::uint64_t> failed_{0};
};

}  // namespace rtc::jobs
