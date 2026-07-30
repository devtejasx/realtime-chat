#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/conversation_service.hpp"

namespace rtc::controllers {

// REST layer for conversations and group management. Every route requires a
// valid JWT. Registers:
//
//   POST   /api/conversations                       — create direct/group
//   GET    /api/conversations                       — list mine (paginated)
//   GET    /api/conversations/<id>                  — get one
//   DELETE /api/conversations/<id>                  — delete (owner/participant)
//   PATCH  /api/conversations/<id>/name             — rename group (owner)
//   POST   /api/conversations/<id>/members          — add member (owner)
//   DELETE /api/conversations/<id>/members/<uid>    — remove member (owner)
//   POST   /api/conversations/<id>/leave            — leave
class ConversationController {
  public:
    ConversationController(services::ConversationService& conversations,
                           middlewares::AuthMiddleware& auth_guard) noexcept
        : conversations_(conversations), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

  private:
    services::ConversationService& conversations_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers
