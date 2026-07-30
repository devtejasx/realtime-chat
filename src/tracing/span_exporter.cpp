#include "rtc/tracing/span_exporter.hpp"

#include <cstdint>

#include <nlohmann/json.hpp>

#include "rtc/logging/logger.hpp"

namespace rtc::tracing {
namespace {

// OTLP encodes SpanKind and StatusCode as the protobuf enum ordinals, and
// JSON-encodes 64-bit integers as strings. Both are spec requirements, not
// stylistic choices, so they live in one place here.
[[nodiscard]] int otlp_kind(SpanKind kind) noexcept {
    switch (kind) {
        case SpanKind::kInternal:
            return 1;
        case SpanKind::kServer:
            return 2;
        case SpanKind::kClient:
            return 3;
        case SpanKind::kProducer:
            return 4;
        case SpanKind::kConsumer:
            return 5;
    }
    return 0;  // SPAN_KIND_UNSPECIFIED
}

[[nodiscard]] int otlp_status(SpanStatus status) noexcept {
    switch (status) {
        case SpanStatus::kUnset:
            return 0;
        case SpanStatus::kOk:
            return 1;
        case SpanStatus::kError:
            return 2;
    }
    return 0;
}

// Zipkin only models remote-facing kinds; an internal span simply omits the
// field, which Zipkin renders as a local operation.
[[nodiscard]] const char* zipkin_kind(SpanKind kind) noexcept {
    switch (kind) {
        case SpanKind::kServer:
            return "SERVER";
        case SpanKind::kClient:
            return "CLIENT";
        case SpanKind::kProducer:
            return "PRODUCER";
        case SpanKind::kConsumer:
            return "CONSUMER";
        case SpanKind::kInternal:
            break;
    }
    return nullptr;
}

[[nodiscard]] std::uint64_t unix_nanos(std::chrono::system_clock::time_point tp) noexcept {
    const auto since = tp.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(since).count());
}

[[nodiscard]] std::uint64_t unix_micros(std::chrono::system_clock::time_point tp) noexcept {
    const auto since = tp.time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(since).count());
}

// OTLP attribute: {"key": k, "value": {"stringValue": v}}. Every attribute is
// exported as a string; typed setters on Span already stringify, which keeps the
// wire format trivial and lossless for the values this service records.
[[nodiscard]] nlohmann::json otlp_attribute(const std::string& key, const std::string& value) {
    return nlohmann::json{{"key", key}, {"value", {{"stringValue", value}}}};
}

[[nodiscard]] nlohmann::json otlp_resource_attributes(const Resource& resource) {
    nlohmann::json attributes = nlohmann::json::array();
    attributes.push_back(otlp_attribute("service.name", resource.service_name));
    if (!resource.service_version.empty()) {
        attributes.push_back(otlp_attribute("service.version", resource.service_version));
    }
    if (!resource.deployment_environment.empty()) {
        attributes.push_back(
            otlp_attribute("deployment.environment", resource.deployment_environment));
    }
    if (!resource.service_instance_id.empty()) {
        attributes.push_back(
            otlp_attribute("service.instance.id", resource.service_instance_id));
    }
    return attributes;
}

}  // namespace

std::string to_otlp_json(const Resource& resource, const std::vector<SpanData>& spans) {
    nlohmann::json encoded = nlohmann::json::array();
    for (const auto& span : spans) {
        const std::uint64_t start = unix_nanos(span.start_time);
        nlohmann::json attributes = nlohmann::json::array();
        for (const auto& [key, value] : span.attributes) {
            attributes.push_back(otlp_attribute(key, value));
        }

        nlohmann::json entry{
            {"traceId", span.trace_id},
            {"spanId", span.span_id},
            {"name", span.name},
            {"kind", otlp_kind(span.kind)},
            // 64-bit values are strings in OTLP/JSON to survive JSON number limits.
            {"startTimeUnixNano", std::to_string(start)},
            {"endTimeUnixNano",
             std::to_string(start + static_cast<std::uint64_t>(span.duration.count()))},
            {"attributes", std::move(attributes)},
            {"status", {{"code", otlp_status(span.status)}}},
        };
        if (!span.parent_span_id.empty()) {
            entry["parentSpanId"] = span.parent_span_id;
        }
        if (!span.trace_state.empty()) {
            entry["traceState"] = span.trace_state;
        }
        if (!span.status_message.empty()) {
            entry["status"]["message"] = span.status_message;
        }
        encoded.push_back(std::move(entry));
    }

    const nlohmann::json body{
        {"resourceSpans",
         nlohmann::json::array(
             {{{"resource", {{"attributes", otlp_resource_attributes(resource)}}},
               {"scopeSpans",
                nlohmann::json::array({{{"scope", {{"name", "rtc"}, {"version", RTC_VERSION}}},
                                        {"spans", std::move(encoded)}}})}}})},
    };
    return body.dump();
}

std::string to_zipkin_json(const Resource& resource, const std::vector<SpanData>& spans) {
    nlohmann::json encoded = nlohmann::json::array();
    for (const auto& span : spans) {
        nlohmann::json tags = nlohmann::json::object();
        for (const auto& [key, value] : span.attributes) {
            tags[key] = value;
        }
        if (span.status == SpanStatus::kError) {
            // Zipkin's convention for a failed span.
            tags["error"] = span.status_message.empty() ? "true" : span.status_message;
        }
        if (!resource.deployment_environment.empty()) {
            tags["deployment.environment"] = resource.deployment_environment;
        }

        nlohmann::json entry{
            {"traceId", span.trace_id},
            {"id", span.span_id},
            {"name", span.name},
            // Zipkin timestamps and durations are microseconds.
            {"timestamp", unix_micros(span.start_time)},
            {"duration", static_cast<std::uint64_t>(span.duration.count()) / 1000U},
            {"localEndpoint", {{"serviceName", resource.service_name}}},
            {"tags", std::move(tags)},
        };
        if (!span.parent_span_id.empty()) {
            entry["parentId"] = span.parent_span_id;
        }
        if (const char* kind = zipkin_kind(span.kind); kind != nullptr) {
            entry["kind"] = kind;
        }
        encoded.push_back(std::move(entry));
    }
    return encoded.dump();
}

void LoggingSpanExporter::export_spans(const Resource& resource,
                                       const std::vector<SpanData>& spans) {
    for (const auto& span : spans) {
        const double millis =
            static_cast<double>(span.duration.count()) / 1'000'000.0;
        RTC_LOG_DEBUG("span service={} trace={} span={} parent={} name='{}' kind={} {:.3f}ms {}",
                      resource.service_name, span.trace_id, span.span_id,
                      span.parent_span_id.empty() ? "-" : span.parent_span_id, span.name,
                      to_string(span.kind), millis, to_string(span.status));
    }
}

}  // namespace rtc::tracing
