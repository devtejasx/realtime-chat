#include "rtc/tracing/trace_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "rtc/utils/random.hpp"

namespace rtc::tracing {
namespace {

[[nodiscard]] bool is_lower_hex(std::string_view value) noexcept {
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// Parses exactly two lowercase hex digits into a byte. Returns nullopt on
// anything else, which keeps the header parser strict about the flags field.
[[nodiscard]] std::optional<std::uint8_t> parse_hex_byte(std::string_view value) noexcept {
    if (value.size() != 2 || !is_lower_hex(value)) {
        return std::nullopt;
    }
    const auto digit = [](char c) -> std::uint8_t {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    return static_cast<std::uint8_t>((digit(value[0]) << 4) | digit(value[1]));
}

// Splits on '-' into at most four fields; extra fields are tolerated (they
// belong to a future traceparent version) and ignored.
[[nodiscard]] std::array<std::string_view, 4> split_fields(std::string_view header,
                                                           std::size_t& count) {
    std::array<std::string_view, 4> fields{};
    count = 0;
    std::size_t begin = 0;
    while (count < fields.size()) {
        const std::size_t dash = header.find('-', begin);
        if (dash == std::string_view::npos) {
            fields[count++] = header.substr(begin);
            return fields;
        }
        fields[count++] = header.substr(begin, dash - begin);
        begin = dash + 1;
    }
    return fields;
}

// Sampling flag: bit 0 of the trace-flags byte.
constexpr std::uint8_t kSampledFlag = 0x01;

}  // namespace

std::string new_trace_id() {
    // 16 bytes -> 32 hex chars. Retry the (astronomically unlikely) all-zero
    // draw rather than emitting an id the spec calls invalid.
    std::string id = utils::generate_hex_token(kTraceIdHexLength / 2);
    if (id == kInvalidTraceId) {
        id = utils::generate_hex_token(kTraceIdHexLength / 2);
    }
    return id;
}

std::string new_span_id() {
    std::string id = utils::generate_hex_token(kSpanIdHexLength / 2);
    if (id == kInvalidSpanId) {
        id = utils::generate_hex_token(kSpanIdHexLength / 2);
    }
    return id;
}

std::optional<SpanContext> parse_traceparent(std::string_view header) {
    if (header.empty()) {
        return std::nullopt;
    }

    std::size_t count = 0;
    const auto fields = split_fields(header, count);
    if (count < 4) {
        return std::nullopt;
    }

    // Field 0: version. Must be two hex digits and not the "ff" sentinel.
    const auto version = parse_hex_byte(fields[0]);
    if (!version || *version == 0xFF) {
        return std::nullopt;
    }

    SpanContext context;
    context.trace_id = std::string(fields[1]);
    context.span_id = std::string(fields[2]);
    if (!is_lower_hex(context.trace_id) || !is_lower_hex(context.span_id) ||
        !context.is_valid()) {
        return std::nullopt;
    }

    const auto flags = parse_hex_byte(fields[3]);
    if (!flags) {
        return std::nullopt;
    }
    context.sampled = (*flags & kSampledFlag) != 0;
    return context;
}

std::string format_traceparent(const SpanContext& context) {
    std::string out;
    out.reserve(55);  // "00-" + 32 + "-" + 16 + "-" + 2
    out += "00-";
    out += context.trace_id;
    out += '-';
    out += context.span_id;
    out += context.sampled ? "-01" : "-00";
    return out;
}

}  // namespace rtc::tracing
