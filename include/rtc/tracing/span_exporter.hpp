#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "rtc/tracing/span.hpp"

namespace rtc::tracing {

// Immutable description of the process emitting spans. Exported once per batch
// as OTLP resource attributes, which is how a backend groups spans by service.
struct Resource {
    std::string service_name = "realtime-chat";
    std::string service_version;
    std::string deployment_environment;
    std::string service_instance_id;  // node id; distinguishes replicas
};

// Export boundary for finished spans.
//
// Implementations must be thread-safe with respect to each other but are only
// ever called from the tracer's single exporter thread, so they may block (that
// is precisely why the export happens off the request path).
class ISpanExporter {
public:
    virtual ~ISpanExporter() = default;

    // Ships a batch. Must not throw: a failed export is a diagnostics problem,
    // never an application error, so implementations log and drop.
    virtual void export_spans(const Resource& resource, const std::vector<SpanData>& spans) = 0;

    // Blocks until in-flight work is flushed. Called on shutdown.
    virtual void flush() {}

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

// Writes each span as one structured log record. Always available (no network
// dependency), which makes it the right default in development and a useful
// fallback when no collector is reachable.
class LoggingSpanExporter final : public ISpanExporter {
public:
    void export_spans(const Resource& resource, const std::vector<SpanData>& spans) override;
    [[nodiscard]] std::string_view name() const noexcept override { return "logging"; }
};

// Discards everything. Used when tracing is disabled so the rest of the
// pipeline needs no null checks.
class NullSpanExporter final : public ISpanExporter {
public:
    void export_spans(const Resource&, const std::vector<SpanData>&) override {}
    [[nodiscard]] std::string_view name() const noexcept override { return "null"; }
};

// Serialises a batch as an OTLP/JSON `ExportTraceServiceRequest` body. Exposed
// separately from the HTTP exporter so the encoding is unit-testable without a
// collector, and reusable by any future transport.
[[nodiscard]] std::string to_otlp_json(const Resource& resource,
                                       const std::vector<SpanData>& spans);

// Serialises a batch as Zipkin API v2 JSON (an array of span objects). Zipkin
// accepts this directly at POST /api/v2/spans.
[[nodiscard]] std::string to_zipkin_json(const Resource& resource,
                                         const std::vector<SpanData>& spans);

}  // namespace rtc::tracing
