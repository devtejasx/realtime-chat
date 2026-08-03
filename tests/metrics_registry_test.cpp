#include "rtc/metrics/metrics_registry.hpp"

#include <gtest/gtest.h>

namespace {

using rtc::metrics::MetricsRegistry;

TEST(MetricsRegistryTest, CountersAccumulate) {
    MetricsRegistry registry;
    registry.increment("rtc_x_total");
    registry.increment("rtc_x_total", 2.0);
    EXPECT_DOUBLE_EQ(registry.counter("rtc_x_total"), 3.0);
}

TEST(MetricsRegistryTest, RenderIncludesCountersAndGauges) {
    MetricsRegistry registry;
    registry.increment("rtc_requests_total", 5);
    registry.set_gauge("rtc_connections", 7);
    const std::string out = registry.render_prometheus();
    EXPECT_NE(out.find("rtc_requests_total 5"), std::string::npos);
    EXPECT_NE(out.find("rtc_connections 7"), std::string::npos);
    EXPECT_NE(out.find("# TYPE rtc_requests_total counter"), std::string::npos);
}

TEST(MetricsRegistryTest, GaugeCallbackEvaluatedAtRender) {
    MetricsRegistry registry;
    int value = 1;
    registry.register_gauge_callback("rtc_live", [&value] { return static_cast<double>(value); });
    value = 42;
    EXPECT_NE(registry.render_prometheus().find("rtc_live 42"), std::string::npos);
}

TEST(MetricsRegistryTest, SummaryEmitsSumCountAvg) {
    MetricsRegistry registry;
    registry.observe("rtc_latency", 2.0);
    registry.observe("rtc_latency", 4.0);
    const std::string out = registry.render_prometheus();
    EXPECT_NE(out.find("rtc_latency_sum 6"), std::string::npos);
    EXPECT_NE(out.find("rtc_latency_count 2"), std::string::npos);
    EXPECT_NE(out.find("rtc_latency_avg 3"), std::string::npos);
}

}  // namespace

// --- histograms ------------------------------------------------------------
//
// observe() used to render only <name>_sum / <name>_count, which answers "what
// is the average" and nothing else. An average latency hides exactly the thing
// worth alerting on: a p99 in seconds is invisible behind a mean in
// milliseconds. Buckets are what make histogram_quantile() — and therefore any
// latency dashboard or SLO — possible.

namespace {

// Extracts the cumulative count for one bucket boundary from the exposition.
[[nodiscard]] std::string bucket_line(const std::string& out,
                                      const std::string& metric,
                                      const std::string& le) {
    const std::string needle = metric + "_bucket{le=\"" + le + "\"} ";
    const auto at = out.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    const auto end = out.find('\n', at);
    return out.substr(at + needle.size(), end - at - needle.size());
}

}  // namespace

TEST(MetricsRegistryTest, ObservationsAreRenderedAsAPrometheusHistogram) {
    rtc::metrics::MetricsRegistry registry;
    registry.observe("rtc_lat", 0.003);  // <= 0.005
    registry.observe("rtc_lat", 0.03);   // <= 0.05
    registry.observe("rtc_lat", 7.0);    // <= 10

    const auto out = registry.render_prometheus();
    EXPECT_NE(out.find("# TYPE rtc_lat histogram"), std::string::npos) << out;
    EXPECT_EQ(bucket_line(out, "rtc_lat", "0.005"), "1");
    EXPECT_EQ(bucket_line(out, "rtc_lat", "0.05"), "2") << "buckets must be cumulative";
    EXPECT_EQ(bucket_line(out, "rtc_lat", "10"), "3");
    EXPECT_EQ(bucket_line(out, "rtc_lat", "+Inf"), "3");
    EXPECT_NE(out.find("rtc_lat_count 3"), std::string::npos);
}

TEST(MetricsRegistryTest, ValuesAboveTheLastBoundOnlyReachInf) {
    rtc::metrics::MetricsRegistry registry;
    registry.observe("rtc_lat", 120.0);  // slower than the 10s top bound

    const auto out = registry.render_prometheus();
    EXPECT_EQ(bucket_line(out, "rtc_lat", "10"), "0");
    EXPECT_EQ(bucket_line(out, "rtc_lat", "+Inf"), "1")
        << "an outlier must still be counted, or _count and the buckets disagree";
}

TEST(MetricsRegistryTest, CustomBucketsReplaceTheLatencyDefaults) {
    // Upload sizes against latency bounds would all land in +Inf.
    rtc::metrics::MetricsRegistry registry;
    registry.register_histogram("rtc_bytes", {1024.0, 1048576.0});
    registry.observe("rtc_bytes", 512.0);
    registry.observe("rtc_bytes", 2048.0);

    const auto out = registry.render_prometheus();
    EXPECT_EQ(bucket_line(out, "rtc_bytes", "1024"), "1");
    EXPECT_EQ(bucket_line(out, "rtc_bytes", "1.04858e+06"), "2");
    EXPECT_EQ(bucket_line(out, "rtc_bytes", "0.005"), "") << "latency defaults must be gone";
}

TEST(MetricsRegistryTest, BucketsAreSortedAndDeduplicated) {
    rtc::metrics::MetricsRegistry registry;
    registry.register_histogram("rtc_x", {10.0, 1.0, 10.0, 5.0});
    registry.observe("rtc_x", 3.0);

    const auto out = registry.render_prometheus();
    // Ascending order is required by the exposition format; a repeated bound
    // would emit a duplicate series.
    const auto one = out.find("rtc_x_bucket{le=\"1\"}");
    const auto five = out.find("rtc_x_bucket{le=\"5\"}");
    const auto ten = out.find("rtc_x_bucket{le=\"10\"}");
    ASSERT_NE(one, std::string::npos);
    EXPECT_LT(one, five);
    EXPECT_LT(five, ten);
    EXPECT_EQ(out.find("rtc_x_bucket{le=\"10\"}", ten + 1), std::string::npos)
        << "duplicate bound emitted twice";
}

TEST(MetricsRegistryTest, AverageIsRetainedAsItsOwnGaugeFamily) {
    // Kept for whatever was already scraping it, but emitted outside the
    // histogram family: <name>_avg is not part of the histogram exposition and a
    // strict parser may reject an unexpected series inside one.
    rtc::metrics::MetricsRegistry registry;
    registry.observe("rtc_lat", 2.0);
    registry.observe("rtc_lat", 4.0);

    const auto out = registry.render_prometheus();
    EXPECT_NE(out.find("# TYPE rtc_lat_avg gauge"), std::string::npos) << out;
    EXPECT_NE(out.find("rtc_lat_avg 3"), std::string::npos);
}
