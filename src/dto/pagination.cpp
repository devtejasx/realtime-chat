#include "rtc/dto/pagination.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

#include "rtc/errors/exceptions.hpp"

namespace rtc::dto {
namespace {

using rtc::errors::ValidationException;

// Parses an integer query parameter, or nullopt when the parameter is absent.
// Throws when present but not a valid integer.
[[nodiscard]] std::optional<std::int64_t> parse_int_param(const crow::request& req,
                                                          const char* name) {
    const char* raw = req.url_params.get(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view view(raw);
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (ec != std::errc{} || ptr != view.data() + view.size()) {
        throw ValidationException("Invalid integer query parameter", std::string("param=") + name);
    }
    return value;
}

}  // namespace

Pagination Pagination::from_request(const crow::request& req) {
    Pagination page;

    if (const auto limit = parse_int_param(req, "limit")) {
        if (*limit < 1) {
            throw ValidationException("limit must be >= 1", "param=limit");
        }
        page.limit = static_cast<int>(std::min<std::int64_t>(*limit, kMaxLimit));
    }
    if (const auto offset = parse_int_param(req, "offset")) {
        if (*offset < 0) {
            throw ValidationException("offset must be >= 0", "param=offset");
        }
        page.offset = *offset;
    }
    page.before_id = parse_int_param(req, "before");
    page.after_id = parse_int_param(req, "after");
    return page;
}

}  // namespace rtc::dto
