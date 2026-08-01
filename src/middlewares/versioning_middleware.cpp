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
    const auto parsed = parse_versioned_path(req.url);
    if (!parsed.has_prefix) {
        // Legacy unversioned call (or a non-/api path): serve it as the default
        // version. This is the backward-compatibility guarantee.
        ctx.version = http::kDefaultApiVersion;
        ctx.explicit_prefix = false;
        return;
    }

    ctx.explicit_prefix = true;

    if (!http::is_supported_api_version(parsed.version)) {
        // Report the version that answered, not the one that was asked for.
        // after_handle stamps X-API-Version from ctx.version, and echoing the
        // unsupported number there would tell the client the server speaks a
        // contract it just refused.
        ctx.version = http::kCurrentApiVersion;

        // Fail closed with a helpful, machine-readable payload rather than a bare
        // route miss.
        //
        // Reaching this point at all depends on the version catch-all registered
        // by the composition root: Crow resolves the route before any middleware
        // runs, so without a rule matching "/api/v<n>/<path>" an unknown version
        // would 404 out of the connection layer and never reach this check.
        res.code = errors::http_status_for(errors::ErrorType::kNotFound);
        res.set_header("Content-Type", "application/json");
        res.set_header(std::string(http::kApiVersionHeader),
                       http::api_version_label(http::kCurrentApiVersion));
        res.body = errors::make_error_body(
                       "unsupported_api_version",
                       "Unsupported API version: " + http::api_version_label(parsed.version),
                       "supported=" + http::supported_api_versions_label())
                       .dump();
        res.end();
        return;
    }

    ctx.version = parsed.version;

    // Deliberately no path rewriting here. Crow's router has already matched by
    // the time before_handle runs (crow/http_connection.h calls handle_initial()
    // from handle_url()), so mutating req.url could not influence routing even in
    // principle — it would only desynchronise the URL the outer access-log and
    // tracing middlewares report. Both prefixes are real registered routes; see
    // rtc/http/route_registrar.hpp.
}

void ApiVersionMiddleware::after_handle(crow::request&, crow::response& res, context& ctx) {
    // Stamped on every response, versioned or not, so a caller can always tell
    // which contract served it.
    res.set_header(std::string(http::kApiVersionHeader), http::api_version_label(ctx.version));
}

}  // namespace rtc::middlewares
