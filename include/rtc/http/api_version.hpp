#pragma once

#include <array>
#include <string>
#include <string_view>

namespace rtc::http {

// API versioning policy for the REST surface.
//
// Design: every route is *authored exactly once*, via RTC_API_ROUTE
// (rtc/http/route_registrar.hpp), which registers it under both the unversioned
// "/api" prefix and the canonical "/api/v1" one. The duplication lives in Crow's
// routing trie rather than in the source, so there is no second copy of a path
// to drift. Adding "/api/v2" costs one entry in kSupportedApiVersions, one more
// prefix in the macro, and only the handlers whose *behaviour* actually differs
// — never a hand-maintained duplicate route table.
//
// Registration time is the only place this can work: Crow resolves the route in
// handle_url() before any middleware runs, so a middleware that rewrote the path
// could never influence matching.
//
// Backward compatibility: the bare "/api" prefix stays permanently supported
// and is treated as an alias for kDefaultApiVersion, so clients written against
// the pre-versioning API keep working byte-for-byte. Responses carry
// kApiVersionHeader so a caller can always tell which contract served it.
//
// Versioning strategy notes for API consumers are in docs/API.md.

// Major versions accepted in a "/api/v<n>/" prefix. Keep ascending.
inline constexpr std::array<int, 1> kSupportedApiVersions{1};

// Version assumed for requests that arrive without a version prefix. This must
// never change: it is the compatibility contract for legacy clients.
inline constexpr int kDefaultApiVersion = 1;

// The newest version this build implements (what new clients should target).
inline constexpr int kCurrentApiVersion = 1;

// Canonical route prefixes.
inline constexpr std::string_view kApiPrefix = "/api";
inline constexpr std::string_view kApiV1Prefix = "/api/v1";

// Response header echoing the version that served the request ("v1").
inline constexpr std::string_view kApiVersionHeader = "X-API-Version";

[[nodiscard]] constexpr bool is_supported_api_version(int version) noexcept {
    for (const int supported : kSupportedApiVersions) {
        if (supported == version) {
            return true;
        }
    }
    return false;
}

// Renders a version as its wire label, e.g. 1 -> "v1".
[[nodiscard]] inline std::string api_version_label(int version) {
    return "v" + std::to_string(version);
}

// Comma-separated list of supported labels, e.g. "v1" — used in error details
// so a client sending /api/v9 learns what it *may* send.
[[nodiscard]] inline std::string supported_api_versions_label() {
    std::string out;
    for (const int version : kSupportedApiVersions) {
        if (!out.empty()) {
            out += ", ";
        }
        out += api_version_label(version);
    }
    return out;
}

}  // namespace rtc::http
