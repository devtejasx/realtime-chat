#include "rtc/realtime/heartbeat_monitor.hpp"

#include <nlohmann/json.hpp>

#include "rtc/logging/logger.hpp"
#include "rtc/realtime/events.hpp"

namespace rtc::realtime {
namespace {

[[nodiscard]] std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

HeartbeatMonitor::HeartbeatMonitor(ConnectionManager& connections, std::chrono::seconds interval,
                                   std::chrono::seconds timeout)
    : connections_(connections), interval_(interval), timeout_(timeout) {}

HeartbeatMonitor::~HeartbeatMonitor() { stop(); }

void HeartbeatMonitor::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;  // already running
    }
    thread_ = std::thread([this] { run(); });
    RTC_LOG_INFO("Heartbeat monitor started (interval={}s, timeout={}s)", interval_.count(),
                 timeout_.count());
}

void HeartbeatMonitor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HeartbeatMonitor::run() {
    const std::int64_t timeout_ms = timeout_.count() * 1000;
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Interruptible wait: wakes early on stop().
            cv_.wait_for(lock, interval_, [this] { return !running_.load(); });
        }
        if (!running_.load()) {
            break;
        }

        const std::int64_t now = now_ms();
        for (const auto& session : connections_.sessions().snapshot()) {
            const std::int64_t idle = now - session->last_activity_ms.load(std::memory_order_relaxed);
            if (idle > timeout_ms) {
                RTC_LOG_DEBUG("Closing idle ws session (user={}, idle={}ms)", session->user_id,
                              idle);
                if (session->connection != nullptr) {
                    session->connection->close("heartbeat timeout");
                }
            } else {
                connections_.send_event(session->connection, realtime::events::kClientPing,
                                        nlohmann::json::object());
            }
        }
    }
    RTC_LOG_INFO("Heartbeat monitor stopped");
}

}  // namespace rtc::realtime
