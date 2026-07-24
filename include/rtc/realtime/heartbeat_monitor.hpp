#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "rtc/realtime/connection_manager.hpp"

namespace rtc::realtime {

// Background monitor that keeps WebSocket connections healthy.
//
// On each tick it snapshots the live sessions and, for any whose last inbound
// activity exceeds the timeout, closes the connection (triggering normal
// disconnect cleanup). Otherwise it sends a lightweight ping so clients and
// intermediaries keep the socket warm. Runs on its own thread with an
// interruptible sleep, so shutdown is immediate.
class HeartbeatMonitor {
public:
    HeartbeatMonitor(ConnectionManager& connections, std::chrono::seconds interval,
                     std::chrono::seconds timeout);
    ~HeartbeatMonitor();

    HeartbeatMonitor(const HeartbeatMonitor&) = delete;
    HeartbeatMonitor& operator=(const HeartbeatMonitor&) = delete;

    void start();
    void stop();

private:
    void run();

    ConnectionManager& connections_;
    std::chrono::seconds interval_;
    std::chrono::seconds timeout_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
};

}  // namespace rtc::realtime
