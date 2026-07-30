#include "rtc/middlewares/versioning_middleware.hpp"

#include <cctype>
#include <charconv>
#include <string>
#include <system_error>

#include "rtc/errors/error_response.hpp"
#include "rtc/errors/error_type.hpp"

namespace rtc::middlewares {
namespace {

// "/api/v" — the literal that must precede the version digits.
constexpr std::string_view kVersionedPrefix = "/api/v";

}  // namespace

ApiVersionRewrite parse_versioned_path(std::string_view path) {
    ApiVersionRewrite out;
    out.normalised = std::string(path);

    if (path.size() <= kVersionedPrefix.size() ||
        path.substr(0, kVersionedPrefix.size()) != kVersionedPrefix) {
        return out;
    }

    // Consume the digit run immediately after "/api/v".
    std::size_t cursor = kVersionedPrefix.size();
    const std::size_t digits_begin = cursor;
    while (cursor < path.size() && std::isdigit(static_cast<unsigned char>(path[cursor])) != 0) {
        ++cursor;
    }
    if (cursor == digits_begin) {
        return out;  // "/api/version..." — not a version segment
    }
    // The segment must end here: either the path ends, or the next character
    // starts a new segment. "/api/v1x/foo" is therefore *not* versioned.
    if (cursor != path.size() && path[cursor] != '/' && path[cursor] != '?') {
        return out;
    }

    int version = 0;
    const char* begin = path.data() + digits_begin;
    const char* end = path.data() + cursor;
    if (const auto [ptr, ec] = std::from_chars(begin, end, version);
        ec != std::errc{} || ptr != end) {
        return out;  // absurdly long digit run; treat as a literal path
    }

    out.has_prefix = true;
    out.version = version;
    // Splice: "/api" + everything from the segment boundary onward. A trailing
    // query string (should the server ever hand us one) survives untouched.
    out.normalised = std::string(http::kApiPrefix);
    out.normalised.append(path.substr(cursor));
    return out;
}

void ApiVersionMiddleware::before_handle(crow::request& req, crow::response& res, context& ctx) {
    ctx.original_path = req.url;

    const auto rewrite = parse_versioned_path(req.url);
    if (!rewrite.has_prefix) {
        // Legacy unversioned call (or a non-/api path): serve it as the default
        // version. This is the backward-compatibility guarantee.
        ctx.version = http::kDefaultApiVersion;
        ctx.explicit_prefix = false;
        return;
    }

    ctx.version = rewrite.version;
    ctx.explicit_prefix = true;

    if (!http::is_supported_api_version(rewrite.version)) {
        // Fail closed with a helpful, machine-readable payload rather than
        // letting the request fall through to a generic route miss.
        res.code = errors::http_status_for(errors::ErrorType::kNotFound);
        res.set_header("Content-Type", "application/json");
        res.set_header(std::string(http::kApiVersionHeader),
                       http::api_version_label(http::kCurrentApiVersion));
        res.body = errors::make_error_body(
                       "unsupported_api_version",
                       "Unsupported API version: " + http::api_version_label(rewrite.version),
                       "supported=" + http::supported_api_versions_label())
                       .dump();
        res.end();
        return;
    }

    // Crow's router matches on req.url only, so rewriting it here is sufficient
    // to make the versioned path resolve to the unversioned route registration.
    req.url = rewrite.normalised;
    ctx.rewritten = true;
}

void ApiVersionMiddleware::after_handle(crow::request& req, crow::response& res, context& ctx) {
    res.set_header(std::string(http::kApiVersionHeader), http::api_version_label(ctx.version));
    if (ctx.rewritten) {
        // Routing is done; hand the client-visible path back so the outer
        // middlewares (access log) report what was actually requested.
        req.url = ctx.original_path;
    }
}

}  // namespace rtc::middlewares
