#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace rtc::metrics {

// Thread-safe metrics registry rendering Prometheus text exposition format.
//
// Supports three instrument kinds:
//   - counters   : monotonic totals (requests, messages, cache hits, ...)
//   - gauges     : point-in-time values, either set directly or pulled at
//                  scrape time via a registered callback (active users, ws
//                  connections, cache hit ratio, memory, uptime)
//   - summaries  : observations recording <name>_sum and <name>_count, from
//                  which average latency and query time are derived
//
// The registry is a process-wide sink injected where needed; it holds no
// business state, only measurements.
class MetricsRegistry {
public:
    MetricsRegistry() : start_(std::chrono::steady_clock::now()) {}

    void increment(const std::string& name, double amount = 1.0);
    void set_gauge(const std::string& name, double value);
    void observe(const std::string& name, double value);

    // Registers a callback evaluated at render time for a live gauge value.
    void register_gauge_callback(const std::string& name, std::function<double()> callback);

    [[nodiscard]] double counter(const std::string& name) const;

    // Seconds since construction.
    [[nodiscard]] double uptime_seconds() const;

    // Renders all metrics in Prometheus text exposition format.
    [[nodiscard]] std::string render_prometheus() const;

private:
    struct Summary {
        double sum = 0.0;
        std::uint64_t count = 0;
    };

    mutable std::mutex mutex_;
    std::map<std::string, double> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, std::function<double()>> gauge_callbacks_;
    std::map<std::string, Summary> summaries_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace rtc::metrics
