#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/reaction_service.hpp"

namespace rtc::controllers {

// REST layer for reactions. All routes require JWT. Registers:
//
//   POST   /api/messages/<id>/reactions   — add/change a reaction
//   DELETE /api/messages/<id>/reactions   — remove the caller's reaction
//   GET    /api/messages/<id>/reactions   — list a message's reactions
class ReactionController {
  public:
    ReactionController(services::ReactionService& reactions,
                       middlewares::AuthMiddleware& auth_guard) noexcept
        : reactions_(reactions), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

  private:
    services::ReactionService& reactions_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers
