#include <gtest/gtest.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/tracing/otlp_http_exporter.hpp"
#include "rtc/tracing/scoped_span.hpp"
#include "rtc/tracing/span_exporter.hpp"
#include "rtc/tracing/tracer.hpp"

namespace {

using rtc::tracing::SpanData;
using rtc::tracing::SpanKind;
using rtc::tracing::SpanStatus;

// Captures exported batches in memory so span content can be asserted on.
class CapturingExporter final : public rtc::tracing::ISpanExporter {
  public:
    void export_spans(const rtc::tracing::Resource& resource,
                      const std::vector<SpanData>& spans) override {
        last_resource = resource;
        for (const auto& span : spans) {
            captured.push_back(span);
        }
    }
    [[nodiscard]] std::string_view name() const noexcept override { return "capturing"; }

    rtc::tracing::Resource last_resource;
    std::vector<SpanData> captured;
};

// Builds a tracer that exports synchronously on stop(), with full sampling.
struct TracerFixture {
    explicit TracerFixture(double ratio = 1.0) {
        auto owned = std::make_unique<CapturingExporter>();
        exporter = owned.get();
        tracer = std::make_unique<rtc::tracing::Tracer>(
            rtc::tracing::Resource{.service_name = "test-service",
                                   .service_version = "9.9.9",
                                   .deployment_environment = "test",
                                   .service_instance_id = "node-1"},
            std::move(owned),
            rtc::tracing::TracerOptions{.enabled = true, .sample_ratio = ratio});
        // Deliberately not started: stop() drains the buffer, which gives the test
        // a deterministic flush point with no background thread involved.
    }

    // Flushes buffered spans into the exporter.
    void flush() { tracer->stop(); }

    CapturingExporter* exporter = nullptr;
    std::unique_ptr<rtc::tracing::Tracer> tracer;
};

// --- W3C trace context ----------------------------------------------------

TEST(TraceContext, ParsesAValidTraceparent) {
    const auto parsed =
        rtc::tracing::parse_traceparent("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->trace_id, "0af7651916cd43dd8448eb211c80319c");
    EXPECT_EQ(parsed->span_id, "b7ad6b7169203331");
    EXPECT_TRUE(parsed->sampled);
    EXPECT_TRUE(parsed->is_valid());
}

TEST(TraceContext, ReadsTheSampledFlag) {
    const auto unsampled =
        rtc::tracing::parse_traceparent("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-00");
    ASSERT_TRUE(unsampled.has_value());
    EXPECT_FALSE(unsampled->sampled);
}

TEST(TraceContext, AcceptsFutureVersions) {
    // The spec requires forward compatibility: a newer version with well-formed
    // leading fields must still be honoured.
    const auto parsed = rtc::tracing::parse_traceparent(
        "01-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01-extrafield");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->trace_id, "0af7651916cd43dd8448eb211c80319c");
}

TEST(TraceContext, RejectsMalformedHeaders) {
    // Each of these must produce a *new* trace rather than a partially-trusted one.
    const char* bad[] = {
        "",
        "not-a-traceparent",
        "00-tooshort-b7ad6b7169203331-01",
        "00-0af7651916cd43dd8448eb211c80319c-tooshort-01",
        "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331",     // missing flags
        "ff-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",  // forbidden version
        "00-00000000000000000000000000000000-b7ad6b7169203331-01",  // all-zero trace id
        "00-0af7651916cd43dd8448eb211c80319c-0000000000000000-01",  // all-zero span id
        "00-0AF7651916CD43DD8448EB211C80319C-b7ad6b7169203331-01",  // uppercase
        "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-zz",  // bad flags
    };
    for (const char* header : bad) {
        EXPECT_FALSE(rtc::tracing::parse_traceparent(header).has_value()) << header;
    }
}

TEST(TraceContext, FormatsAndRoundTrips) {
    rtc::tracing::SpanContext context;
    context.trace_id = "0af7651916cd43dd8448eb211c80319c";
    context.span_id = "b7ad6b7169203331";
    context.sampled = true;

    const std::string header = rtc::tracing::format_traceparent(context);
    EXPECT_EQ(header, "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");

    const auto parsed = rtc::tracing::parse_traceparent(header);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->trace_id, context.trace_id);
    EXPECT_EQ(parsed->span_id, context.span_id);
    EXPECT_TRUE(parsed->sampled);
}

TEST(TraceContext, GeneratesValidIds) {
    for (int i = 0; i < 32; ++i) {
        const std::string trace_id = rtc::tracing::new_trace_id();
        const std::string span_id = rtc::tracing::new_span_id();
        EXPECT_EQ(trace_id.size(), rtc::tracing::kTraceIdHexLength);
        EXPECT_EQ(span_id.size(), rtc::tracing::kSpanIdHexLength);
        EXPECT_NE(trace_id, rtc::tracing::kInvalidTraceId);
        EXPECT_NE(span_id, rtc::tracing::kInvalidSpanId);
    }
}

// --- span lifecycle -------------------------------------------------------

TEST(Tracer, DisabledTracerReturnsAnInertSpan) {
    rtc::tracing::Tracer tracer(rtc::tracing::Resource{},
                                std::make_unique<rtc::tracing::NullSpanExporter>(),
                                rtc::tracing::TracerOptions{.enabled = false});
    auto span = tracer.start_span("noop");
    EXPECT_FALSE(span.is_recording());
    // Every method must remain safe on an inert span; that is what lets call sites
    // hold one unconditionally.
    EXPECT_NO_THROW(span.set_attribute("k", std::string("v")));
    EXPECT_NO_THROW(span.set_status(SpanStatus::kOk));
    EXPECT_NO_THROW(span.end());
}

TEST(Tracer, RecordsASpanOnDestruction) {
    TracerFixture fixture;
    {
        auto span = fixture.tracer->start_span("handler", SpanKind::kServer);
        ASSERT_TRUE(span.is_recording());
        span.set_attribute("http.request.method", std::string("GET"));
        span.set_attribute("http.response.status_code", static_cast<std::int64_t>(200));
        span.set_status(SpanStatus::kOk);
    }  // RAII: destruction ends and submits
    fixture.flush();

    ASSERT_EQ(fixture.exporter->captured.size(), 1U);
    const auto& span = fixture.exporter->captured.front();
    EXPECT_EQ(span.name, "handler");
    EXPECT_EQ(span.kind, SpanKind::kServer);
    EXPECT_EQ(span.status, SpanStatus::kOk);
    EXPECT_TRUE(span.parent_span_id.empty()) << "a span with no parent must be a root";
    EXPECT_EQ(span.attributes.size(), 2U);
}

TEST(Tracer, ParentsASpanToAnInboundContext) {
    TracerFixture fixture;
    rtc::tracing::SpanContext parent;
    parent.trace_id = "0af7651916cd43dd8448eb211c80319c";
    parent.span_id = "b7ad6b7169203331";
    parent.sampled = true;

    {
        auto span = fixture.tracer->start_span("child", SpanKind::kServer, &parent);
    }
    fixture.flush();

    ASSERT_EQ(fixture.exporter->captured.size(), 1U);
    const auto& span = fixture.exporter->captured.front();
    // Continuing the upstream trace is the whole point of context propagation.
    EXPECT_EQ(span.trace_id, parent.trace_id);
    EXPECT_EQ(span.parent_span_id, parent.span_id);
}

TEST(Tracer, InheritsAnUnsampledParentDecision) {
    // A trace must never be captured in fragments: if the caller did not sample,
    // neither do we, regardless of our own ratio.
    TracerFixture fixture(/*ratio=*/1.0);
    rtc::tracing::SpanContext parent;
    parent.trace_id = "0af7651916cd43dd8448eb211c80319c";
    parent.span_id = "b7ad6b7169203331";
    parent.sampled = false;

    {
        auto span = fixture.tracer->start_span("child", SpanKind::kServer, &parent);
        EXPECT_FALSE(span.is_recording());
    }
    fixture.flush();
    EXPECT_TRUE(fixture.exporter->captured.empty());
}

TEST(Tracer, ZeroRatioSamplesNothing) {
    TracerFixture fixture(/*ratio=*/0.0);
    for (int i = 0; i < 20; ++i) {
        auto span = fixture.tracer->start_span("root");
        EXPECT_FALSE(span.is_recording());
    }
    fixture.flush();
    EXPECT_TRUE(fixture.exporter->captured.empty());
    EXPECT_EQ(fixture.tracer->spans_started(), 20U);
}

TEST(Tracer, RecordsAnErrorWithItsMessage) {
    TracerFixture fixture;
    {
        auto span = fixture.tracer->start_span("failing");
        span.record_error("database unavailable");
    }
    fixture.flush();

    ASSERT_EQ(fixture.exporter->captured.size(), 1U);
    const auto& span = fixture.exporter->captured.front();
    EXPECT_EQ(span.status, SpanStatus::kError);
    EXPECT_EQ(span.status_message, "database unavailable");
}

TEST(Tracer, ExportsResourceAttributes) {
    TracerFixture fixture;
    {
        auto span = fixture.tracer->start_span("x");
    }
    fixture.flush();
    EXPECT_EQ(fixture.exporter->last_resource.service_name, "test-service");
    EXPECT_EQ(fixture.exporter->last_resource.service_instance_id, "node-1");
}

// --- scoped spans and implicit parenting ----------------------------------

TEST(ScopedSpan, NoActiveContextOutsideAnyScope) {
    EXPECT_EQ(rtc::tracing::current_span_context(), nullptr);
}

TEST(ScopedSpan, ActivatesAndRestoresTheThreadContext) {
    TracerFixture fixture;
    rtc::tracing::set_tracer(fixture.tracer.get());

    {
        auto outer = rtc::tracing::trace_scope("outer", SpanKind::kServer);
        const auto* active = rtc::tracing::current_span_context();
        ASSERT_NE(active, nullptr);
        const std::string outer_span_id = active->span_id;

        {
            // A nested scope must parent itself to the enclosing span — this is what
            // lets a repository call attach to a request span with no plumbing.
            auto inner = rtc::tracing::db_scope("select_user");
            const auto* nested = rtc::tracing::current_span_context();
            ASSERT_NE(nested, nullptr);
            EXPECT_NE(nested->span_id, outer_span_id);
            EXPECT_EQ(nested->trace_id, active->trace_id);
        }

        // The previous context is restored on scope exit.
        EXPECT_EQ(rtc::tracing::current_span_context()->span_id, outer_span_id);
    }

    EXPECT_EQ(rtc::tracing::current_span_context(), nullptr);
    rtc::tracing::set_tracer(nullptr);
    fixture.flush();

    ASSERT_EQ(fixture.exporter->captured.size(), 2U);
    // The child is reported first (it ends first), and must name the outer as parent.
    const auto& child = fixture.exporter->captured[0];
    const auto& parent = fixture.exporter->captured[1];
    EXPECT_EQ(child.name, "db select_user");
    EXPECT_EQ(child.kind, SpanKind::kClient);
    EXPECT_EQ(child.parent_span_id, parent.span_id);
    EXPECT_EQ(child.trace_id, parent.trace_id);
}

TEST(ScopedSpan, DbScopeAppliesSemanticConventionsWithoutSql) {
    TracerFixture fixture;
    rtc::tracing::set_tracer(fixture.tracer.get());
    {
        auto scope = rtc::tracing::db_scope("insert_message");
    }
    rtc::tracing::set_tracer(nullptr);
    fixture.flush();

    ASSERT_EQ(fixture.exporter->captured.size(), 1U);
    const auto& span = fixture.exporter->captured.front();
    bool has_system = false;
    for (const auto& [key, value] : span.attributes) {
        if (key == "db.system") {
            has_system = true;
            EXPECT_EQ(value, "postgresql");
        }
        // SQL text can embed user content and must never reach a trace backend.
        EXPECT_NE(key, "db.statement");
    }
    EXPECT_TRUE(has_system);
}

TEST(Tracer, UninstalledProviderYieldsInertSpans) {
    rtc::tracing::set_tracer(nullptr);
    auto scope = rtc::tracing::trace_scope("orphan");
    EXPECT_FALSE(scope.span().is_recording());
    EXPECT_EQ(rtc::tracing::current_span_context(), nullptr);
}

// --- exporters ------------------------------------------------------------

TEST(SpanExporter, EncodesOtlpJson) {
    SpanData span;
    span.name = "GET /api/v1/messages";
    span.kind = SpanKind::kServer;
    span.trace_id = "0af7651916cd43dd8448eb211c80319c";
    span.span_id = "b7ad6b7169203331";
    span.parent_span_id = "aaaaaaaaaaaaaaaa";
    span.status = SpanStatus::kOk;
    span.duration = std::chrono::nanoseconds(1'500'000);
    span.attributes.emplace_back("http.request.method", "GET");

    const auto json = nlohmann::json::parse(
        rtc::tracing::to_otlp_json(rtc::tracing::Resource{.service_name = "svc"}, {span}));

    const auto& resource_spans = json.at("resourceSpans").at(0);
    EXPECT_EQ(resource_spans.at("resource").at("attributes").at(0).at("key"), "service.name");

    const auto& encoded = resource_spans.at("scopeSpans").at(0).at("spans").at(0);
    EXPECT_EQ(encoded.at("traceId"), span.trace_id);
    EXPECT_EQ(encoded.at("spanId"), span.span_id);
    EXPECT_EQ(encoded.at("parentSpanId"), span.parent_span_id);
    EXPECT_EQ(encoded.at("kind"), 2);               // SPAN_KIND_SERVER
    EXPECT_EQ(encoded.at("status").at("code"), 1);  // STATUS_CODE_OK
    // OTLP/JSON requires 64-bit values as strings.
    EXPECT_TRUE(encoded.at("startTimeUnixNano").is_string());
    EXPECT_TRUE(encoded.at("endTimeUnixNano").is_string());
}

TEST(SpanExporter, EncodesZipkinJson) {
    SpanData span;
    span.name = "db select";
    span.kind = SpanKind::kClient;
    span.trace_id = "0af7651916cd43dd8448eb211c80319c";
    span.span_id = "b7ad6b7169203331";
    span.status = SpanStatus::kError;
    span.status_message = "timeout";
    span.duration = std::chrono::nanoseconds(2'000'000);  // 2 ms

    const auto json = nlohmann::json::parse(
        rtc::tracing::to_zipkin_json(rtc::tracing::Resource{.service_name = "svc"}, {span}));

    ASSERT_TRUE(json.is_array());
    const auto& encoded = json.at(0);
    EXPECT_EQ(encoded.at("id"), span.span_id);
    EXPECT_EQ(encoded.at("kind"), "CLIENT");
    EXPECT_EQ(encoded.at("localEndpoint").at("serviceName"), "svc");
    EXPECT_EQ(encoded.at("duration"), 2000);  // microseconds
    EXPECT_EQ(encoded.at("tags").at("error"), "timeout");
    EXPECT_FALSE(encoded.contains("parentId"));
}

TEST(SpanExporter, ZipkinOmitsKindForInternalSpans) {
    SpanData span;
    span.name = "internal";
    span.kind = SpanKind::kInternal;
    span.trace_id = "0af7651916cd43dd8448eb211c80319c";
    span.span_id = "b7ad6b7169203331";

    const auto json =
        nlohmann::json::parse(rtc::tracing::to_zipkin_json(rtc::tracing::Resource{}, {span}));
    EXPECT_FALSE(json.at(0).contains("kind"));
}

// --- collector endpoint parsing -------------------------------------------

TEST(OtlpEndpoint, ParsesFullUrl) {
    const auto parsed = rtc::tracing::parse_endpoint("http://collector:4318/v1/traces",
                                                     rtc::tracing::TraceWireFormat::kOtlpHttpJson);
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.host, "collector");
    EXPECT_EQ(parsed.port, "4318");
    EXPECT_EQ(parsed.target, "/v1/traces");
}

TEST(OtlpEndpoint, AppliesOtlpDefaults) {
    const auto parsed =
        rtc::tracing::parse_endpoint("collector", rtc::tracing::TraceWireFormat::kOtlpHttpJson);
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.port, "4318");
    EXPECT_EQ(parsed.target, "/v1/traces");
}

TEST(OtlpEndpoint, AppliesZipkinDefaults) {
    const auto parsed =
        rtc::tracing::parse_endpoint("zipkin-host", rtc::tracing::TraceWireFormat::kZipkinJson);
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.port, "9411");
    EXPECT_EQ(parsed.target, "/api/v2/spans");
}

TEST(OtlpEndpoint, HonoursAnExplicitPortWithoutAPath) {
    const auto parsed =
        rtc::tracing::parse_endpoint("10.0.0.5:9999", rtc::tracing::TraceWireFormat::kOtlpHttpJson);
    EXPECT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.host, "10.0.0.5");
    EXPECT_EQ(parsed.port, "9999");
    EXPECT_EQ(parsed.target, "/v1/traces");
}

TEST(OtlpEndpoint, RejectsAnEmptyEndpoint) {
    EXPECT_FALSE(
        rtc::tracing::parse_endpoint("", rtc::tracing::TraceWireFormat::kOtlpHttpJson).valid);
    EXPECT_FALSE(
        rtc::tracing::parse_endpoint("http://", rtc::tracing::TraceWireFormat::kOtlpHttpJson)
            .valid);
}

TEST(OtlpEndpoint, ParsesWireFormatNames) {
    EXPECT_EQ(rtc::tracing::parse_wire_format("zipkin"),
              rtc::tracing::TraceWireFormat::kZipkinJson);
    EXPECT_EQ(rtc::tracing::parse_wire_format("otlp"),
              rtc::tracing::TraceWireFormat::kOtlpHttpJson);
    // Unknown names fall back to the more capable format rather than failing.
    EXPECT_EQ(rtc::tracing::parse_wire_format("nonsense"),
              rtc::tracing::TraceWireFormat::kOtlpHttpJson);
}

}  // namespace
