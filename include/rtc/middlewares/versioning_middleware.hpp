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

// Classifies "/api/v2/messages" as version 2 and reports "/api/messages" as its
// unversioned equivalent.
//
// Only a *complete* "v" + digits path segment counts, so "/api/version" and
// "/api/v1x/foo" are classified as unversioned and routed literally. Paths
// outside the "/api" namespace (e.g. "/health", "/metrics", "/docs") never carry
// a version: operational endpoints are deliberately unversioned.
//
// `normalised` is informational — it names the legacy path a versioned request
// corresponds to, which the version catch-all uses when explaining a 404. It is
// no longer fed back into the router; see below.
[[nodiscard]] ApiVersionRewrite parse_versioned_path(std::string_view path);

// Global middleware implementing the API-versioning policy in rtc/http/api_version.hpp.
//
// Responsibilities:
//
//   1. Determine which version served the request, from an explicit
//      "/api/v<n>/..." prefix or by falling back to the default for a legacy
//      unversioned call.
//   2. Reject an unknown version with a 404 and a machine-readable error naming
//      the versions this build supports — far friendlier than a bare route miss.
//   3. Stamp X-API-Version on every response so clients can assert the contract
//      they were served, including on legacy unversioned calls.
//
// What this middleware explicitly does *not* do is rewrite the request path.
// An earlier revision normalised "/api/v1/x" to "/api/x" here so a single
// unversioned route table could serve both prefixes. That never worked: Crow
// resolves the route in handle_url() -> handle_initial(), before any middleware
// runs, so every versioned path 404'd out of the connection layer with the
// rewrite still pending. Both prefixes are now registered as real routes via
// RTC_API_ROUTE (rtc/http/route_registrar.hpp), which is the only place the
// router will actually look.
//
// Placement: this middleware sits *last* in the App's middleware list, so it is
// the innermost before_handle and the first after_handle to run. When an
// unsupported version is rejected, Crow still unwinds the *outer* middlewares,
// so the 404 keeps its CORS/security headers, request metrics and access-log
// line instead of silently bypassing them.
struct ApiVersionMiddleware {
    struct context {
        int version = http::kDefaultApiVersion;
        bool explicit_prefix = false;  // false => legacy unversioned call
    };

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);
};

}  // namespace rtc::middlewares
