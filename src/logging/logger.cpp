#include "rtc/logging/logger.hpp"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

#include "rtc/logging/log_context.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::logging {
namespace {

constexpr const char* kLoggerName = "realtime-chat";

// Correlation is emitted through custom pattern flags rather than by editing log
// statements.
//
// The alternative — passing ids into each RTC_LOG_* call — guarantees that the
// lines which matter during an incident are the ones nobody remembered to
// annotate. Reading the ambient context in the formatter means every existing
// statement, including ones written before tracing existed, becomes correlated
// without being touched.
//
//   %*  trace id   (32 hex chars, or empty when not tracing)
//   %&  span id    (16 hex chars, or empty)
//   %~  request id (from the logging middleware, or empty)
//
// Those three characters are unused by spdlog's own pattern vocabulary.
constexpr char kTraceIdFlag = '*';
constexpr char kSpanIdFlag = '&';
constexpr char kRequestIdFlag = '~';

// Human-readable: 2026-07-24 12:00:00.123 [info] [tid 12345] realtime-chat: msg
//
// Correlation is appended in brackets and only when present, so single-instance
// development output stays as readable as it was.
constexpr const char* kTextPattern = "%Y-%m-%d %H:%M:%S.%e [%^%l%$] [tid %t] %n:%~%* %v";

// One JSON object per line for structured log ingestion. (spdlog does not escape
// %v; log call sites avoid embedding raw quotes/newlines in messages.)
//
// trace_id/span_id/request_id are always present as keys, empty when there is no
// active context. A stable schema is worth more to a log pipeline than a compact
// one: a field that appears only sometimes forces every query to handle its
// absence.
constexpr const char* kJsonPattern =
    R"({"time":"%Y-%m-%dT%H:%M:%S.%e%z","level":"%l","thread":%t,"logger":"%n",)"
    R"("trace_id":"%*","span_id":"%&","request_id":"%~","message":"%v"})";

[[nodiscard]] std::string to_lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// --- custom pattern flags --------------------------------------------------

// Emits the active trace id, or nothing. `bracketed` is used by the text pattern
// so a line without a trace does not carry an empty "[]".
class TraceIdFlag final : public spdlog::custom_flag_formatter {
  public:
    explicit TraceIdFlag(bool bracketed) noexcept : bracketed_(bracketed) {}

    void format(const spdlog::details::log_msg&,
                const std::tm&,
                spdlog::memory_buf_t& dest) override {
        const auto* context = tracing::current_span_context();
        if (context == nullptr || !context->is_valid()) {
            return;
        }
        if (bracketed_) {
            dest.push_back(' ');
            dest.push_back('[');
        }
        dest.append(context->trace_id.data(), context->trace_id.data() + context->trace_id.size());
        if (bracketed_) {
            dest.push_back(']');
        }
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<TraceIdFlag>(bracketed_);
    }

  private:
    bool bracketed_;
};

class SpanIdFlag final : public spdlog::custom_flag_formatter {
  public:
    void format(const spdlog::details::log_msg&,
                const std::tm&,
                spdlog::memory_buf_t& dest) override {
        const auto* context = tracing::current_span_context();
        if (context == nullptr || !context->is_valid()) {
            return;
        }
        dest.append(context->span_id.data(), context->span_id.data() + context->span_id.size());
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<SpanIdFlag>();
    }
};

class RequestIdFlag final : public spdlog::custom_flag_formatter {
  public:
    explicit RequestIdFlag(bool bracketed) noexcept : bracketed_(bracketed) {}

    void format(const spdlog::details::log_msg&,
                const std::tm&,
                spdlog::memory_buf_t& dest) override {
        const auto request_id = current_request_id();
        if (request_id.empty()) {
            return;
        }
        if (bracketed_) {
            dest.push_back(' ');
            dest.push_back('[');
        }
        dest.append(request_id.data(), request_id.data() + request_id.size());
        if (bracketed_) {
            dest.push_back(']');
        }
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<RequestIdFlag>(bracketed_);
    }

  private:
    bool bracketed_;
};

}  // namespace

std::unique_ptr<spdlog::formatter> make_formatter(bool json) {
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<TraceIdFlag>(kTraceIdFlag, !json);
    formatter->add_flag<SpanIdFlag>(kSpanIdFlag);
    formatter->add_flag<RequestIdFlag>(kRequestIdFlag, !json);
    formatter->set_pattern(json ? kJsonPattern : kTextPattern);
    return formatter;
}

spdlog::level::level_enum parse_level(std::string_view level) noexcept {
    const std::string normalized = to_lower(level);
    if (normalized == "trace")
        return spdlog::level::trace;
    if (normalized == "debug")
        return spdlog::level::debug;
    if (normalized == "info")
        return spdlog::level::info;
    if (normalized == "warn" || normalized == "warning")
        return spdlog::level::warn;
    if (normalized == "error" || normalized == "err")
        return spdlog::level::err;
    if (normalized == "critical" || normalized == "crit")
        return spdlog::level::critical;
    if (normalized == "off")
        return spdlog::level::off;
    return spdlog::level::info;
}

void init(std::string_view level, std::string_view format) {
    // Replace any previously registered logger so re-init is idempotent.
    spdlog::drop(kLoggerName);

    auto logger = spdlog::stdout_color_mt(kLoggerName);
    // set_formatter rather than set_pattern: the custom flags have to be
    // registered on the formatter object before the pattern that uses them is
    // compiled, and set_pattern would build a default formatter that does not
    // know them.
    logger->set_formatter(make_formatter(to_lower(format) == "json"));

    const auto lvl = parse_level(level);
    logger->set_level(lvl);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
}

std::shared_ptr<spdlog::logger> get() {
    if (auto logger = spdlog::get(kLoggerName)) {
        return logger;
    }
    return spdlog::default_logger();
}

void shutdown() {
    if (auto logger = spdlog::get(kLoggerName)) {
        logger->flush();
    }
    spdlog::shutdown();
}

}  // namespace rtc::logging
