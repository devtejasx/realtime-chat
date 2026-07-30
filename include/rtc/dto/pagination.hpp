#pragma once

#include <crow/http_request.h>

#include <cstdint>
#include <optional>

namespace rtc::dto {

// Pagination parameters shared by list endpoints.
//
// Supports classic limit/offset plus a cursor-ready keyset design: `before_id`
// / `after_id` let callers page by message id (id < before_id, id > after_id),
// which stays efficient and stable under inserts — unlike deep offsets. Values
// are parsed from the query string and clamped to safe bounds.
struct Pagination {
    static constexpr int kDefaultLimit = 50;
    static constexpr int kMaxLimit = 100;

    int limit = kDefaultLimit;
    std::int64_t offset = 0;
    std::optional<std::int64_t> before_id;  // keyset cursor: return ids < before_id
    std::optional<std::int64_t> after_id;   // keyset cursor: return ids > after_id

    // Parses ?limit=&offset=&before=&after= from the request, applying defaults
    // and clamping (limit to [1, kMaxLimit], offset to >= 0). Non-numeric values
    // throw rtc::errors::ValidationException.
    [[nodiscard]] static Pagination from_request(const crow::request& req);

    [[nodiscard]] bool uses_cursor() const noexcept {
        return before_id.has_value() || after_id.has_value();
    }
};

}  // namespace rtc::dto
