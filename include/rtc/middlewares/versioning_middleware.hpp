#pragma once

#include <crow/http_request.h>
#include <crow/http_response.h>

#include <string>
#include <string_view>

#include "rtc/http/api_version.hpp"

namespace rtc::middlewares {

// Outcome of inspecting a request path for an "/api/v<n>" prefix.
//
// Exposed (rather than kept private to the middleware) so the parsing rule —
// the one piece of real logic here — is directly unit-testable without spinning
// up a Crow app. See tests/api_versioning_test.cpp.
struct ApiVersionRewrite {
    bool has_prefix = false;  // the path carried an explicit /api/v<n> prefix
    int version = 0;          // parsed major version; meaningful only if has_prefix
    std::string normalised;   // path with the version segment reduced to "/api"
};

// Reduces "/api/v2/messages" to "/api/messages" and reports the version.
//
// Only a *complete* "v" + digits path segment counts, so "/api/version" and
// "/api/v1x/foo" are left untouched and routed literally. Paths outside the
// "/api" namespace (e.g. "/health", "/metrics", "/docs") are never rewritten:
// operational endpoints are deliberately unversioned.
[[nodiscard]] ApiVersionRewrite parse_versioned_path(std::string_view path);

// Global middleware implementing the API-versioning policy in rtc/http/api_version.hpp.
//
// Responsibilities:
//
//   1. Normalise "/api/v<n>/..." to "/api/..." before routing, which is what
//      lets one route table serve every version with zero duplication.
//   2. Reject an unknown version with a 404 and a machine-readable error naming
//      the versions this build supports — far friendlier than a bare route miss.
//   3. Stamp X-API-Version on every response so clients can assert the contract
//      they were served, including on legacy unversioned calls.
//
// Placement: this middleware sits *last* in the App's middleware list, which
// means it is the innermost before_handle (still ahead of the router, so the
// rewrite takes effect) and the first after_handle to run. Two properties fall
// out of that, both deliberate:
//
//   - When an unsupported version is rejected, Crow still unwinds the *outer*
//     middlewares, so the 404 keeps its CORS/security headers, request metrics
//     and access-log line instead of silently bypassing them.
//   - after_handle restores the original path before the logging middleware
//     unwinds, so both access-log lines show the URL the client actually
//     requested rather than the internally rewritten one.
struct ApiVersionMiddleware {
    struct context {
        int version = http::kDefaultApiVersion;
        bool explicit_prefix = false;  // false => legacy unversioned call
        std::string original_path;
        bool rewritten = false;
    };

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);
};

}  // namespace rtc::middlewares
