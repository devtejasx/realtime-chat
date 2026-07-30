#pragma once

#include <crow/http_request.h>
#include <crow/http_response.h>

#include <string>

namespace rtc::middlewares {

// Global middleware applying OWASP-aligned security headers to every response
// and handling CORS (including preflight). The allowed-origins policy is
// configured at startup via set_allowed_origins(). A value of "*" allows any
// origin; otherwise the exact configured value is echoed.
struct SecurityMiddleware {
    struct context {};

    void set_allowed_origins(std::string origins) { allowed_origins_ = std::move(origins); }

    void before_handle(crow::request& req, crow::response& res, context& /*ctx*/) {
        // Short-circuit CORS preflight with the appropriate headers.
        if (req.method == crow::HTTPMethod::Options) {
            apply_cors(res);
            res.code = 204;
            res.end();
        }
    }

    // Marker header a handler may set to request its own Content-Security-Policy.
    //
    // The default policy (`default-src 'none'`) is right for a JSON API but blocks
    // any HTML page that needs assets — the Swagger UI viewer, in practice.
    // after_handle runs *after* the handler, so a handler cannot simply set the
    // header itself; it would be overwritten. This marker inverts the control:
    // the handler states its intended policy, the middleware honours it, and the
    // marker is stripped so it never reaches the client.
    //
    // Deliberately narrow: it can only replace CSP, never remove any of the other
    // security headers, so the blast radius of a misuse is one directive.
    static constexpr const char* kCspOverrideHeader = "X-RTC-CSP-Override";

    void after_handle(crow::request& /*req*/, crow::response& res, context& /*ctx*/) {
        apply_cors(res);
        // Security headers (defense-in-depth for an API surface).
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("X-XSS-Protection", "0");

        const std::string requested = res.get_header_value(kCspOverrideHeader);
        if (requested.empty()) {
            res.set_header("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
        } else {
            res.set_header("Content-Security-Policy", requested);
            res.headers.erase(kCspOverrideHeader);
        }
    }

  private:
    void apply_cors(crow::response& res) const {
        res.set_header("Access-Control-Allow-Origin", allowed_origins_);
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        res.set_header("Access-Control-Max-Age", "600");
    }

    std::string allowed_origins_ = "*";
};

}  // namespace rtc::middlewares
