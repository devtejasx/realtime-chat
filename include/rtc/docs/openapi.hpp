#pragma once

#include <string>
#include <string_view>

namespace rtc::docs {

// The OpenAPI 3.1 description of this service's REST surface.
//
// Compiled in rather than read from disk. That is a deliberate trade:
//
//   * The spec can never be missing or stale relative to the binary — a container
//     image that forgot to COPY a docs directory would otherwise serve a 404 from
//     /openapi.json, and the failure would only be noticed by a human.
//   * /docs works in a scratch/distroless image with no filesystem layout at all.
//
// The cost is that editing the spec means recompiling one translation unit, which
// is the right price for the guarantee.
//
// Returns the raw JSON document. The pointed-to storage is a string literal with
// static lifetime, so the view is valid for the life of the process.
[[nodiscard]] std::string_view openapi_json() noexcept;

// The spec with `servers[0].url` set to `base_url`, so "Try it out" in Swagger UI
// targets the deployment actually being viewed rather than a hardcoded host.
[[nodiscard]] std::string openapi_json_for(std::string_view base_url);

// A self-contained Swagger UI page.
//
// Assets are loaded from a CDN, which requires the page's Content-Security-Policy
// to permit it — the global policy is `default-src 'none'`, so DocsController
// overrides CSP for this one response and nothing else. `spec_url` is the path the
// page fetches the document from.
[[nodiscard]] std::string swagger_ui_html(std::string_view spec_url);

// The exact CSP the Swagger UI page needs: still restrictive, but permitting the
// CDN's script/style and the inline bootstrap that starts the viewer.
[[nodiscard]] std::string_view swagger_ui_csp() noexcept;

}  // namespace rtc::docs
