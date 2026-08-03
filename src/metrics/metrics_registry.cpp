#include "rtc/metrics/metrics_registry.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace rtc::metrics {

const std::vector<double>& MetricsRegistry::default_buckets() {
    // Prometheus' own default latency boundaries. Chosen rather than invented:
    // dashboards, alert templates and examples across the ecosystem assume them,
    // and matching means a stock latency panel works without re-binning.
    static const std::vector<double> kDefaults = {
        0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    return kDefaults;
}

MetricsRegistry::Histogram& MetricsRegistry::histogram_for(const std::string& name) {
    auto& histogram = histograms_[name];
    if (histogram.bounds.empty()) {
        histogram.bounds = default_buckets();
        histogram.bucket_counts.assign(histogram.bounds.size(), 0);
    }
    return histogram;
}

void MetricsRegistry::increment(const std::string& name, double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[name] += amount;
}

void MetricsRegistry::set_gauge(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    gauges_[name] = value;
}

void MetricsRegistry::register_histogram(const std::string& name, std::vector<double> buckets) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::sort(buckets.begin(), buckets.end());
    buckets.erase(std::unique(buckets.begin(), buckets.end()), buckets.end());

    auto& histogram = histograms_[name];
    histogram.bounds = std::move(buckets);
    // Counts are reset rather than carried over: they were binned under the old
    // boundaries, and re-binning them is not possible from aggregates alone.
    // Silently keeping them would misreport the distribution.
    histogram.bucket_counts.assign(histogram.bounds.size(), 0);
    histogram.sum = 0.0;
    histogram.count = 0;
}

void MetricsRegistry::observe(const std::string& name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& histogram = histogram_for(name);
    histogram.sum += value;
    histogram.count += 1;

    // Counts land in the single bucket they belong to; render_prometheus
    // accumulates them into the cumulative form Prometheus expects. Storing
    // non-cumulatively keeps an observation O(log n) rather than O(n) in the
    // number of bounds, which matters on a hot path like request latency.
    const auto upper = std::lower_bound(histogram.bounds.begin(), histogram.bounds.end(), value);
    if (upper != histogram.bounds.end()) {
        const auto index = static_cast<std::size_t>(upper - histogram.bounds.begin());
        histogram.bucket_counts[index] += 1;
    }
    // Values above the last bound fall into +Inf, which is implicit: rendering
    // emits it from `count`, so nothing is stored for it here.
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
    for (const auto& [name, histogram] : histograms_) {
        out << "# TYPE " << name << " histogram\n";

        // Cumulative: each bucket reports everything at or below its bound,
        // which is what histogram_quantile() interpolates over.
        std::uint64_t cumulative = 0;
        for (std::size_t i = 0; i < histogram.bounds.size(); ++i) {
            cumulative += histogram.bucket_counts[i];
            out << name << "_bucket{le=\"" << histogram.bounds[i] << "\"} " << cumulative << '\n';
        }
        out << name << "_bucket{le=\"+Inf\"} " << histogram.count << '\n'
            << name << "_sum " << histogram.sum << '\n'
            << name << "_count " << histogram.count << '\n';

        // Retained from the previous summary rendering for backward
        // compatibility. Emitted as its own gauge family rather than inside the
        // histogram above, because `<name>_avg` is not part of the histogram
        // exposition and a parser is entitled to reject an unexpected series in
        // a metric family. Derivable as rate(_sum)/rate(_count); kept because
        // something may already be scraping it.
        const double avg =
            histogram.count == 0 ? 0.0 : histogram.sum / static_cast<double>(histogram.count);
        out << "# TYPE " << name << "_avg gauge\n" << name << "_avg " << avg << '\n';
    }
    return out.str();
}

}  // namespace rtc::metrics
