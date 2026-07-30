#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "rtc/tracing/span_exporter.hpp"

namespace rtc::tracing {

// Wire format the collector endpoint expects.
enum class TraceWireFormat {
    kOtlpHttpJson,  // POST /v1/traces  — OpenTelemetry Collector, Jaeger >= 1.35, Tempo
    kZipkinJson,    // POST /api/v2/spans — Zipkin, and Jaeger's Zipkin-compatible port
};

[[nodiscard]] std::string_view to_string(TraceWireFormat format) noexcept;

// Parses a format name from configuration ("otlp" / "otlp_http" / "zipkin").
// Unrecognised values fall back to OTLP, the more capable default.
[[nodiscard]] TraceWireFormat parse_wire_format(std::string_view value) noexcept;

// Ships span batches to a trace collector over plain HTTP/1.1.
//
// Why a hand-rolled exporter rather than the OpenTelemetry C++ SDK: that SDK
// pulls in gRPC/protobuf and a substantial build-time surface, which is a poor
// trade for a service that only needs to POST a JSON batch every few seconds.
// Both supported encodings are stable, documented wire formats, so the output is
// consumable by any OTLP or Zipkin backend — Jaeger, Zipkin, Tempo, or an
// OpenTelemetry Collector fanning out to several of them.
//
// Transport notes:
//   - Asio is used directly (already a dependency via Crow), one short-lived
//     connection per batch. Batches are seconds apart, so connection reuse would
//     buy nothing and holding an idle socket open costs a collector resource.
//   - Every network operation is bounded by a timeout. A slow or dead collector
//     therefore delays telemetry and nothing else; it can never block the
//     request path, which runs on different threads entirely.
//   - Failures are logged (rate-limited) and the batch is dropped. Telemetry is
//     best-effort by design.
//   - HTTP only, no TLS. Collectors belong on the private network or behind a
//     local sidecar/agent — the standard deployment. Point the exporter at an
//     agent on localhost if the collector is remote.
class OtlpHttpSpanExporter final : public ISpanExporter {
public:
    struct Options {
        std::string endpoint;  // "http://host:4318/v1/traces" or host:port form
        TraceWireFormat format = TraceWireFormat::kOtlpHttpJson;
        std::chrono::milliseconds timeout{3000};
    };

    explicit OtlpHttpSpanExporter(Options options);
    ~OtlpHttpSpanExporter() override;

    OtlpHttpSpanExporter(const OtlpHttpSpanExporter&) = delete;
    OtlpHttpSpanExporter& operator=(const OtlpHttpSpanExporter&) = delete;

    void export_spans(const Resource& resource, const std::vector<SpanData>& spans) override;
    void flush() override;
    [[nodiscard]] std::string_view name() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Endpoint broken into its parts. Exposed for unit testing the URL parser,
// which is the only fiddly logic in the exporter.
struct ParsedEndpoint {
    std::string host;
    std::string port = "4318";
    std::string target = "/v1/traces";
    bool valid = false;
};

// Parses "http://host:4318/v1/traces", "host:4318" or "host" into its parts,
// defaulting the port and path per the selected wire format.
[[nodiscard]] ParsedEndpoint parse_endpoint(std::string_view endpoint, TraceWireFormat format);

}  // namespace rtc::tracing
