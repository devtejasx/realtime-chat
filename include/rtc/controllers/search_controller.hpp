#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/search_service.hpp"

namespace rtc::controllers {

// REST layer for full-text search. Requires a valid JWT. Registers:
//
//   GET /api/search/messages — ?q=&conversation_id=&sender_id=&from=&to=
//                              &fuzzy=&highlight=&limit=&offset=
//
// Results are always scoped to conversations the caller participates in; that
// restriction lives in the SQL (see IMessageSearchRepository), not here, so it
// cannot be bypassed by a controller mistake.
class SearchController {
public:
    SearchController(services::SearchService& search,
                    middlewares::AuthMiddleware& auth_guard) noexcept
        : search_(search), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

private:
    services::SearchService& search_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers
