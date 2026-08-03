#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "rtc/logging/log_context.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/tracing/scoped_span.hpp"
#include "rtc/tracing/tracer.hpp"

// Log correlation.
//
// Traces and logs are only useful together. Before this, the JSON pattern
// carried time, level, thread, logger and message — and nothing that tied a line
// to the request or trace that produced it. An operator with a slow trace in
// Jaeger had no way to find the corresponding log lines except by timestamp,
// which is guesswork the moment there is more than one request in flight.
//
// These tests assert the correlation fields are populated from ambient context
// rather than from arguments at the call site. That distinction is the whole
// design: instrumenting per call site guarantees the lines that matter during an
// incident are the ones nobody remembered to annotate.

namespace {

using nlohmann::json;

// Captures formatted output by installing the project's formatter on a sink
// writing to a string, so what is asserted is the real pattern, not a copy.
class CapturedLog {
  public:
    explicit CapturedLog(bool json_format) {
        sink_ = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream_);
        logger_ = std::make_shared<spdlog::logger>("capture", sink_);
        logger_->set_level(spdlog::level::trace);
        // Reuses production init to build the formatter, then borrows it: this
        // must break if the pattern loses a field.
        logger_->set_formatter(rtc::logging::make_formatter(json_format));
    }

    void info(const std::string& message) {
        logger_->info(message);
        logger_->flush();
    }

    [[nodiscard]] std::string text() const { return stream_.str(); }

    [[nodiscard]] json last_json() const {
        auto out = stream_.str();
        const auto last = out.find_last_of('\n');
        if (last != std::string::npos && last + 1 == out.size()) {
            out.erase(last);
        }
        const auto start = out.find_last_of('\n');
        const auto line = start == std::string::npos ? out : out.substr(start + 1);
        return json::parse(line, nullptr, /*allow_exceptions=*/false);
    }

  private:
    std::ostringstream stream_;
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink_;
    std::shared_ptr<spdlog::logger> logger_;
};

// --- JSON schema -----------------------------------------------------------

TEST(LogCorrelation, JsonAlwaysCarriesTheCorrelationKeys) {
    // Present even when empty. A key that appears only sometimes forces every
    // downstream query to handle its absence, which is a worse contract than an
    // empty string.
    CapturedLog log(/*json_format=*/true);
    log.info("no context here");

    const auto entry = log.last_json();
    ASSERT_FALSE(entry.is_discarded()) << "log line is not valid JSON: " << log.text();
    for (const char* key :
         {"time", "level", "thread", "logger", "message", "trace_id", "span_id", "request_id"}) {
        EXPECT_TRUE(entry.contains(key)) << "missing key: " << key;
    }
    EXPECT_EQ(entry.at("trace_id"), "");
    EXPECT_EQ(entry.at("request_id"), "");
    EXPECT_EQ(entry.at("message"), "no context here");
}

// --- request id ------------------------------------------------------------

TEST(LogCorrelation, RequestIdIsPickedUpFromAmbientContext) {
    CapturedLog log(/*json_format=*/true);
    {
        const rtc::logging::RequestIdScope scope("req-abc123");
        // Deliberately no id passed to the call — the point is that a statement
        // written without any awareness of correlation still gets it.
        log.info("handling something");
    }

    const auto entry = log.last_json();
    ASSERT_FALSE(entry.is_discarded());
    EXPECT_EQ(entry.at("request_id"), "req-abc123");
}

TEST(LogCorrelation, RequestIdIsRestoredWhenTheScopeEnds) {
    EXPECT_TRUE(rtc::logging::current_request_id().empty());
    {
        const rtc::logging::RequestIdScope outer("outer");
        EXPECT_EQ(rtc::logging::current_request_id(), "outer");
        {
            const rtc::logging::RequestIdScope inner("inner");
            EXPECT_EQ(rtc::logging::current_request_id(), "inner");
        }
        // Restored, not cleared. A nested scope must not strip the id from the
        // work that continues after it.
        EXPECT_EQ(rtc::logging::current_request_id(), "outer");
    }
    EXPECT_TRUE(rtc::logging::current_request_id().empty());
}

TEST(LogCorrelation, ExplicitSetAndClearBehaveLikeTheScope) {
    // The path LoggingMiddleware takes, since Crow needs a move-assignable
    // context and cannot hold a guard.
    rtc::logging::set_request_id("req-explicit");
    EXPECT_EQ(rtc::logging::current_request_id(), "req-explicit");
    rtc::logging::clear_request_id();
    EXPECT_TRUE(rtc::logging::current_request_id().empty());
}

// --- trace and span ids ----------------------------------------------------

TEST(LogCorrelation, TraceAndSpanIdsComeFromTheActiveSpan) {
    rtc::tracing::Tracer tracer(rtc::tracing::Resource{.service_name = "test"},
                                nullptr,
                                rtc::tracing::TracerOptions{.enabled = true, .sample_ratio = 1.0});
    rtc::tracing::set_tracer(&tracer);

    CapturedLog log(/*json_format=*/true);
    std::string trace_id;
    std::string span_id;
    {
        auto scope = rtc::tracing::trace_scope("unit-test-span");
        const auto* context = rtc::tracing::current_span_context();
        ASSERT_NE(context, nullptr) << "no active span context";
        trace_id = context->trace_id;
        span_id = context->span_id;
        log.info("inside a span");
    }
    rtc::tracing::set_tracer(nullptr);

    const auto entry = log.last_json();
    ASSERT_FALSE(entry.is_discarded());
    EXPECT_EQ(entry.at("trace_id"), trace_id)
        << "log line cannot be joined to its trace in Jaeger/Tempo";
    EXPECT_EQ(entry.at("span_id"), span_id);
    EXPECT_EQ(entry.at("trace_id").get<std::string>().size(), 32U) << "W3C trace id is 32 hex";
    EXPECT_EQ(entry.at("span_id").get<std::string>().size(), 16U) << "W3C span id is 16 hex";
}

TEST(LogCorrelation, NoActiveSpanLeavesTraceFieldsEmptyRatherThanInvalid) {
    // Emitting a zeroed trace id would be worse than emitting none: it looks
    // like a real trace and matches nothing.
    CapturedLog log(/*json_format=*/true);
    log.info("outside any span");

    const auto entry = log.last_json();
    ASSERT_FALSE(entry.is_discarded());
    EXPECT_EQ(entry.at("trace_id"), "");
    EXPECT_EQ(entry.at("span_id"), "");
}

// --- text format -----------------------------------------------------------

TEST(LogCorrelation, TextFormatStaysReadableWithoutContext) {
    // Development output must not sprout empty brackets for every line logged
    // outside a request.
    CapturedLog log(/*json_format=*/false);
    log.info("plain line");

    const auto out = log.text();
    EXPECT_NE(out.find("plain line"), std::string::npos);
    EXPECT_EQ(out.find("[]"), std::string::npos) << "empty correlation brackets: " << out;
}

TEST(LogCorrelation, TextFormatShowsTheRequestIdWhenThereIsOne) {
    CapturedLog log(/*json_format=*/false);
    {
        const rtc::logging::RequestIdScope scope("req-text");
        log.info("with context");
    }
    EXPECT_NE(log.text().find("[req-text]"), std::string::npos) << log.text();
}

// --- backward compatibility ------------------------------------------------

TEST(LogCorrelation, ExistingLevelParsingIsUnchanged) {
    EXPECT_EQ(rtc::logging::parse_level("debug"), spdlog::level::debug);
    EXPECT_EQ(rtc::logging::parse_level("WARN"), spdlog::level::warn);
    EXPECT_EQ(rtc::logging::parse_level("nonsense"), spdlog::level::info);
}

}  // namespace
