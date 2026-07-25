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
