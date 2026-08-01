#pragma once

#include <crow/app.h>

#include <string>
#include <type_traits>
#include <utility>

#include "rtc/http/api_version.hpp"

namespace rtc::http {

// ---------------------------------------------------------------------------
// Versioned route registration.
//
// Why this exists — and why the obvious alternative does not work.
//
// The API is served at two prefixes: the canonical "/api/v1/..." documented in
// the OpenAPI spec, and the legacy unversioned "/api/..." kept alive for
// existing clients. The tempting way to get that for free is to normalise
// "/api/v1/x" down to "/api/x" in a middleware's before_handle and register the
// route table only once, unversioned.
//
// That is exactly what this project used to do, and it silently served a 404
// for every versioned path. Crow resolves the route *before* any middleware
// runs. In crow/http_connection.h, handle_url() calls:
//
//     routing_handle_result_ = handler_->handle_initial(req_, res);
//     if (!routing_handle_result_->rule_index) { ... complete_request(); }
//
// The router has already matched (or failed to match) req.url by the time
// before_handle is invoked, and a miss short-circuits the request without ever
// reaching the handler chain. Rewriting req.url in a middleware therefore
// mutates a string nobody will consult again for routing purposes.
//
// Rather than reaching into Crow's connection internals to preprocess the URL
// earlier — which would couple this service to private framework machinery and
// break on any upstream change — the alias is materialised where the router can
// actually see it: at registration time. RTC_API_ROUTE registers one handler
// under both prefixes, so the route table itself contains both paths and
// ordinary trie matching does the rest.
//
// Properties this preserves:
//
//   - Controllers keep declaring each endpoint exactly once. The duplication
//     lives in the trie, not in the source, so there is no second copy of a
//     path to drift out of sync.
//   - No dependence on Crow internals. Only the documented rule API
//     (methods/name/handler) is used.
//   - Backward compatibility is structural rather than conventional: the legacy
//     prefix is a real registered route, not a rewrite that can stop firing.
//
// The version segment is still *parsed* by ApiVersionMiddleware, which stamps
// X-API-Version and rejects versions this build does not support. That
// middleware no longer rewrites anything; it only reports and enforces policy.
// ---------------------------------------------------------------------------

// A handle to the same endpoint registered under several path prefixes.
//
// Mirrors the subset of Crow's rule builder the controllers actually use, and
// forwards each call to every underlying rule. Returned by value from
// RTC_API_ROUTE as a temporary, which lives to the end of the full expression —
// long enough for the usual `.methods(...)(handler)` chain.
template <typename Rule>
class VersionedRule {
  public:
    VersionedRule(Rule& canonical, Rule& versioned) noexcept
        : canonical_(&canonical), versioned_(&versioned) {}

    // Crow's single-argument overload assigns the method mask and the variadic
    // one ORs into it; forwarding the pack verbatim preserves either semantic.
    template <typename... Methods>
    VersionedRule& methods(Methods... methods) {
        canonical_->methods(methods...);
        versioned_->methods(methods...);
        return *this;
    }

    // Rule names surface in Crow's diagnostics, where two identically named
    // rules would be indistinguishable; the alias is suffixed so a duplicate
    // -handler error names the prefix that actually failed to validate.
    VersionedRule& name(std::string value) {
        canonical_->name(value);
        versioned_->name(value + " [versioned]");
        return *this;
    }

    // Each rule needs its own handler instance: Crow's TaggedRule::operator()
    // captures by move, so handing the same object to both would leave the
    // second registration holding a moved-from callable.
    template <typename Func>
    void operator()(Func&& handler) {
        using Handler = std::decay_t<Func>;
        (*canonical_)(Handler(handler));
        (*versioned_)(Handler(std::forward<Func>(handler)));
    }

  private:
    Rule* canonical_;
    Rule* versioned_;
};

template <typename Rule>
[[nodiscard]] inline VersionedRule<Rule> make_versioned_rule(Rule& canonical,
                                                             Rule& versioned) noexcept {
    return VersionedRule<Rule>(canonical, versioned);
}

// RTC_API_ROUTE materialises the "/api/v1" alias as a literal prefix, because
// Crow derives a route's parameter tag from a compile-time string. Adding a
// version to kSupportedApiVersions therefore requires extending the macro to
// register that prefix as well — this assertion makes forgetting a build error
// rather than another silent 404.
static_assert(kSupportedApiVersions.size() == 1 && kSupportedApiVersions[0] == 1,
              "The supported API version set changed. RTC_API_ROUTE registers '/api/v1' as a "
              "literal alias; register the new prefix there (and extend VersionedRule to hold "
              "more than two rules) before updating this assertion.");

}  // namespace rtc::http

// Registers one handler under both the canonical versioned prefix and the
// legacy unversioned one.
//
// Usage mirrors CROW_ROUTE, but `path` is the segment *after* the API prefix:
//
//     RTC_API_ROUTE(app, "/auth/register")
//         .methods(crow::HTTPMethod::Post)([this](const crow::request& req) { ... });
//
// registers both "/api/auth/register" and "/api/v1/auth/register".
//
// Both CROW_ROUTE expansions produce the same TaggedRule<> specialisation: the
// two prefixes differ only in literal text, so they carry identical parameter
// tags. Their relative registration order is unspecified (function arguments
// are unsequenced) but immaterial — the paths cannot collide with each other,
// and both are registered before the version catch-all installed by the
// composition root.
#define RTC_API_ROUTE(app, path)                                     \
    ::rtc::http::make_versioned_rule(CROW_ROUTE((app), "/api" path), \
                                     CROW_ROUTE((app), "/api/v1" path))
