#pragma once

#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>

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

    void after_handle(crow::request& /*req*/, crow::response& res, context& /*ctx*/) {
        apply_cors(res);
        // Security headers (defense-in-depth for an API surface).
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
        res.set_header("Referrer-Policy", "no-referrer");
        res.set_header("X-XSS-Protection", "0");
        res.set_header("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
    }

private:
    void apply_cors(crow::response& res) const {
        res.set_header("Access-Control-Allow-Origin", allowed_origins_);
        res.set_header("Access-Control-Allow-Methods",
                       "GET, POST, PUT, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        res.set_header("Access-Control-Max-Age", "600");
    }

    std::string allowed_origins_ = "*";
};

}  // namespace rtc::middlewares
