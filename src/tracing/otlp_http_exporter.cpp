#include "rtc/tracing/otlp_http_exporter.hpp"

#include <asio/buffer.hpp>
#include <asio/connect.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/streambuf.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <cstdint>
#include <exception>
#include <istream>
#include <system_error>
#include <utility>

#include "rtc/logging/logger.hpp"

namespace rtc::tracing {
namespace {

using asio::ip::tcp;

constexpr std::string_view kHttpScheme = "http://";

// Defaults per wire format: OTLP/HTTP listens on 4318 at /v1/traces, Zipkin on
// 9411 at /api/v2/spans.
struct FormatDefaults {
    std::string_view port;
    std::string_view target;
};

[[nodiscard]] FormatDefaults defaults_for(TraceWireFormat format) noexcept {
    if (format == TraceWireFormat::kZipkinJson) {
        return {"9411", "/api/v2/spans"};
    }
    return {"4318", "/v1/traces"};
}

// Runs `io` until every pending operation completes or `timeout` elapses.
// Returns false on timeout, in which case the socket is closed to cancel the
// outstanding operation. This is how the exporter stays bounded without pulling
// in coroutines or a steady_timer per call.
[[nodiscard]] bool run_bounded(asio::io_context& io,
                               std::chrono::milliseconds timeout,
                               tcp::socket& socket) {
    io.restart();
    io.run_for(timeout);
    if (!io.stopped()) {
        std::error_code ignored;
        socket.close(ignored);
        io.run();  // let the cancelled handlers complete
        return false;
    }
    return true;
}

}  // namespace

std::string_view to_string(TraceWireFormat format) noexcept {
    return format == TraceWireFormat::kZipkinJson ? "zipkin" : "otlp";
}

TraceWireFormat parse_wire_format(std::string_view value) noexcept {
    if (value == "zipkin" || value == "zipkin_json") {
        return TraceWireFormat::kZipkinJson;
    }
    return TraceWireFormat::kOtlpHttpJson;
}

ParsedEndpoint parse_endpoint(std::string_view endpoint, TraceWireFormat format) {
    const FormatDefaults defaults = defaults_for(format);
    ParsedEndpoint out;
    out.port = std::string(defaults.port);
    out.target = std::string(defaults.target);

    std::string_view rest = endpoint;
    // `>=`, not `>`: with a strict comparison the bare string "http://" fails the
    // length test, the scheme is left in place, and the parser then happily reads
    // "http" as the host — accepting an endpoint that is plainly unusable.
    if (rest.size() >= kHttpScheme.size() && rest.substr(0, kHttpScheme.size()) == kHttpScheme) {
        rest.remove_prefix(kHttpScheme.size());
    }
    if (rest.empty()) {
        return out;  // invalid
    }

    // Split off the path first so a colon inside it is not mistaken for a port.
    if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
        out.target = std::string(rest.substr(slash));
        rest = rest.substr(0, slash);
    }
    if (const std::size_t colon = rest.rfind(':'); colon != std::string_view::npos) {
        const std::string_view port = rest.substr(colon + 1);
        if (!port.empty()) {
            out.port = std::string(port);
        }
        rest = rest.substr(0, colon);
    }
    if (rest.empty()) {
        return out;  // invalid: no host
    }

    out.host = std::string(rest);
    out.valid = true;
    return out;
}

struct OtlpHttpSpanExporter::Impl {
    explicit Impl(Options opts)
        : options(std::move(opts)),
          endpoint(parse_endpoint(options.endpoint, options.format)),
          label(std::string(to_string(options.format)) + "-http") {}

    // Performs one bounded POST. Returns false on any failure; the caller logs.
    [[nodiscard]] bool post(const std::string& body) {
        asio::io_context io;
        tcp::resolver resolver(io);
        tcp::socket socket(io);

        std::error_code resolve_ec;
        tcp::resolver::results_type endpoints;
        resolver.async_resolve(endpoint.host,
                               endpoint.port,
                               [&](const std::error_code& ec, tcp::resolver::results_type r) {
                                   resolve_ec = ec;
                                   endpoints = std::move(r);
                               });
        if (!run_bounded(io, options.timeout, socket) || resolve_ec) {
            return false;
        }

        std::error_code connect_ec;
        asio::async_connect(
            socket, endpoints, [&](const std::error_code& ec, const tcp::endpoint&) {
                connect_ec = ec;
            });
        if (!run_bounded(io, options.timeout, socket) || connect_ec) {
            return false;
        }

        const std::string request = "POST " + endpoint.target +
                                    " HTTP/1.1\r\n"
                                    "Host: " +
                                    endpoint.host + ":" + endpoint.port +
                                    "\r\n"
                                    "Content-Type: application/json\r\n"
                                    "Content-Length: " +
                                    std::to_string(body.size()) +
                                    "\r\n"
                                    "Connection: close\r\n"
                                    "\r\n" +
                                    body;

        std::error_code write_ec;
        asio::async_write(socket,
                          asio::buffer(request),
                          [&](const std::error_code& ec, std::size_t) { write_ec = ec; });
        if (!run_bounded(io, options.timeout, socket) || write_ec) {
            return false;
        }

        // Read the status line so a rejecting collector is visible in the logs.
        // The rest of the response is irrelevant and deliberately not drained.
        asio::streambuf response;
        std::error_code read_ec;
        asio::async_read_until(
            socket, response, "\r\n", [&](const std::error_code& ec, std::size_t) {
                read_ec = ec;
            });
        if (!run_bounded(io, options.timeout, socket)) {
            return false;
        }
        if (read_ec && read_ec != asio::error::eof) {
            return false;
        }

        std::istream stream(&response);
        std::string http_version;
        int status = 0;
        stream >> http_version >> status;
        if (status < 200 || status >= 300) {
            RTC_LOG_WARN("Trace collector rejected batch: HTTP {} from {}:{}{}",
                         status,
                         endpoint.host,
                         endpoint.port,
                         endpoint.target);
            return false;
        }
        return true;
    }

    Options options;
    ParsedEndpoint endpoint;
    std::string label;
    // Failures are logged on the first occurrence and then every 100th, so an
    // unreachable collector cannot flood the log it shares with the application.
    std::atomic<std::uint64_t> consecutive_failures{0};
};

OtlpHttpSpanExporter::OtlpHttpSpanExporter(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
    if (!impl_->endpoint.valid) {
        RTC_LOG_WARN("Invalid trace collector endpoint '{}'; spans will not be exported",
                     impl_->options.endpoint);
    } else {
        RTC_LOG_INFO("Trace exporter: {} -> {}:{}{}",
                     impl_->label,
                     impl_->endpoint.host,
                     impl_->endpoint.port,
                     impl_->endpoint.target);
    }
}

OtlpHttpSpanExporter::~OtlpHttpSpanExporter() = default;

std::string_view OtlpHttpSpanExporter::name() const noexcept {
    return impl_->label;
}

void OtlpHttpSpanExporter::export_spans(const Resource& resource,
                                        const std::vector<SpanData>& spans) {
    if (spans.empty() || !impl_->endpoint.valid) {
        return;
    }

    // ISpanExporter forbids throwing: a telemetry failure must never surface as
    // an application error, so everything below is contained here.
    try {
        const std::string body = impl_->options.format == TraceWireFormat::kZipkinJson
                                     ? to_zipkin_json(resource, spans)
                                     : to_otlp_json(resource, spans);
        if (impl_->post(body)) {
            impl_->consecutive_failures.store(0, std::memory_order_relaxed);
            return;
        }
    } catch (const std::exception& ex) {
        RTC_LOG_DEBUG("Span export threw: {}", ex.what());
    } catch (...) {
        RTC_LOG_DEBUG("Span export threw an unknown exception");
    }

    const auto failures = impl_->consecutive_failures.fetch_add(1, std::memory_order_relaxed) + 1;
    if (failures == 1 || failures % 100 == 0) {
        RTC_LOG_WARN("Dropped {} span(s): trace collector {}:{} unreachable ({} consecutive)",
                     spans.size(),
                     impl_->endpoint.host,
                     impl_->endpoint.port,
                     failures);
    }
}

void OtlpHttpSpanExporter::flush() {
    // Each export is synchronous and self-contained, so there is nothing queued
    // inside the exporter to flush. Present to satisfy the interface contract.
}

}  // namespace rtc::tracing
