#pragma once

#include "rtc/config/config.hpp"
#include "rtc/http/app.hpp"

namespace rtc::controllers {

// Serves the API description and an interactive viewer:
//
//   GET /openapi.json          — the OpenAPI 3.1 document
//   GET /api/v1/openapi.json   — same document, versioned path
//   GET /docs                  — Swagger UI
//
// Unauthenticated. The document describes the shape of the API, not any data in
// it, and a reference that requires a token is a reference nobody reads. In a
// deployment where even that is too much, block /docs and /openapi.json at the
// reverse proxy — nginx/conf.d/realtime-chat.conf shows the pattern used for
// /metrics.
//
// Production gate: when RTC_DOCS_ENABLED is explicitly set to a falsey value the
// routes still exist but return 404, so the endpoints can be switched off without
// a rebuild or an nginx change.
class DocsController {
  public:
    explicit DocsController(const config::Config& config) noexcept : config_(config) {}

    void register_routes(http::App& app);

  private:
    // Reconstructs the caller-visible origin ("http://host:port") from the request
    // so Swagger UI's "Try it out" targets this deployment rather than a hardcoded
    // localhost. Honours X-Forwarded-Proto/Host when behind a reverse proxy.
    [[nodiscard]] static std::string origin_of(const crow::request& req);

    const config::Config& config_;
};

}  // namespace rtc::controllers
