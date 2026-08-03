#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace rtc::metrics {

// Thread-safe metrics registry rendering Prometheus text exposition format.
//
// Supports three instrument kinds:
//   - counters   : monotonic totals (requests, messages, cache hits, ...)
//   - gauges     : point-in-time values, either set directly or pulled at
//                  scrape time via a registered callback (active users, ws
//                  connections, cache hit ratio, memory, uptime)
//   - histograms : observations recording <name>_bucket{le=...}, <name>_sum and
//                  <name>_count, from which percentiles are derived
//
// The registry is a process-wide sink injected where needed; it holds no
// business state, only measurements.
class MetricsRegistry {
  public:
    MetricsRegistry() : start_(std::chrono::steady_clock::now()) {}

    void increment(const std::string& name, double amount = 1.0);
    void set_gauge(const std::string& name, double value);
    void observe(const std::string& name, double value);

    // Overrides the bucket boundaries for one histogram.
    //
    // Needed because the metrics here measure genuinely different quantities:
    // the default boundaries are latency-shaped (5ms to 10s), which put every
    // upload size in the +Inf bucket and make the histogram useless. Buckets are
    // sorted and de-duplicated; +Inf is implicit and never supplied.
    //
    // Call before the first observation. Re-registering afterwards discards the
    // counts already accumulated, since they were bucketed under different
    // boundaries and cannot be re-binned.
    void register_histogram(const std::string& name, std::vector<double> buckets);

    // Prometheus' own default latency boundaries, in seconds.
    [[nodiscard]] static const std::vector<double>& default_buckets();

    // Registers a callback evaluated at render time for a live gauge value.
    void register_gauge_callback(const std::string& name, std::function<double()> callback);

    [[nodiscard]] double counter(const std::string& name) const;

    // Seconds since construction.
    [[nodiscard]] double uptime_seconds() const;

    // Renders all metrics in Prometheus text exposition format.
    [[nodiscard]] std::string render_prometheus() const;

  private:
    struct Histogram {
        double sum = 0.0;
        std::uint64_t count = 0;
        std::vector<double> bounds;                // ascending, excludes +Inf
        std::vector<std::uint64_t> bucket_counts;  // per-bound; cumulative at render
    };

    // Callers must hold mutex_.
    [[nodiscard]] Histogram& histogram_for(const std::string& name);

    mutable std::mutex mutex_;
    std::map<std::string, double> counters_;
    std::map<std::string, double> gauges_;
    std::map<std::string, std::function<double()>> gauge_callbacks_;
    std::map<std::string, Histogram> histograms_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace rtc::metrics
