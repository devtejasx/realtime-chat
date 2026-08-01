#include "rtc/controllers/search_controller.hpp"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/dto/pagination.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"

namespace rtc::controllers {
namespace {

// Parses an optional integer query parameter. A present-but-malformed value is a
// client error rather than something to silently ignore — quietly dropping a
// filter would return a *wider* result set than asked for, which is the wrong
// failure direction for anything scoped by permissions.
[[nodiscard]] std::optional<std::int64_t> optional_int_param(const crow::request& req,
                                                             const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view text(raw);
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        throw errors::ValidationException(std::string("Invalid integer for parameter: ") + name,
                                          std::string("field=") + name);
    }
    return value;
}

// Parses an optional boolean query parameter, defaulting when absent.
[[nodiscard]] bool bool_param(const crow::request& req, const char* name, bool fallback) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return fallback;
    }
    const std::string_view text(raw);
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

[[nodiscard]] std::string required_string_param(const crow::request& req, const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        throw errors::ValidationException(std::string("Missing required parameter: ") + name,
                                          std::string("field=") + name);
    }
    return std::string(raw);
}

}  // namespace

void SearchController::register_routes(http::App& app) {
    // Reachable as both /api/search/messages and /api/v1/search/messages: the
    // macro registers the handler under each prefix.
    RTC_API_ROUTE(app, "/search/messages")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);

                repositories::MessageSearchQuery query;
                query.term = required_string_param(req, "q");
                query.conversation_id = optional_int_param(req, "conversation_id");
                query.sender_id = optional_int_param(req, "sender_id");
                query.from_epoch = optional_int_param(req, "from");
                query.to_epoch = optional_int_param(req, "to");
                query.fuzzy = bool_param(req, "fuzzy", /*fallback=*/true);
                query.highlight = bool_param(req, "highlight", /*fallback=*/true);

                const auto page = dto::Pagination::from_request(req);
                return http::json_response(200,
                                           search_.search_messages(claims.user_id, query, page));
            });
        });
}

}  // namespace rtc::controllers
