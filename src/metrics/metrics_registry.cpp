#include "rtc/metrics/metrics_registry.hpp"

#include <sstream>
#include <utility>

namespace rtc::metrics {

void MetricsRegistry::increment(const std::string& name, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += amount;
}

void MetricsRegistry::set_gauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
}

void MetricsRegistry::observe(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& summary = summaries_[name];
    summary.sum += value;
    summary.count += 1;
}

void MetricsRegistry::register_gauge_callback(const std::string& name,
                                              std::function<double()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauge_callbacks_[name] = std::move(callback);
}

double MetricsRegistry::counter(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = counters_.find(name);
    return it == counters_.end() ? 0.0 : it->second;
}

double MetricsRegistry::uptime_seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

std::string MetricsRegistry::render_prometheus() const {
    std::ostringstream out;
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [name, value] : counters_) {
        out << "# TYPE " << name << " counter\n" << name << ' ' << value << '\n';
    }
    for (const auto& [name, value] : gauges_) {
        out << "# TYPE " << name << " gauge\n" << name << ' ' << value << '\n';
    }
    for (const auto& [name, callback] : gauge_callbacks_) {
        out << "# TYPE " << name << " gauge\n" << name << ' ' << callback() << '\n';
    }
    for (const auto& [name, summary] : summaries_) {
        const double avg =
            summary.count == 0 ? 0.0 : summary.sum / static_cast<double>(summary.count);
        out << "# TYPE " << name << " summary\n"
            << name << "_sum " << summary.sum << '\n'
            << name << "_count " << summary.count << '\n'
            << name << "_avg " << avg << '\n';
    }
    return out.str();
}

}  // namespace rtc::metrics
