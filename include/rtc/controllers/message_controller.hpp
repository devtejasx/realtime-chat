#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/message_service.hpp"

namespace rtc::controllers {

// REST layer for messages. Every route requires a valid JWT. Registers:
//
//   POST   /api/messages        — send a message (persists + broadcasts)
//   GET    /api/messages        — list/search (?conversation_id=&q=&sender_id=&limit=&...)
//   PATCH  /api/messages/<id>   — edit (author only)
//   DELETE /api/messages/<id>   — soft-delete (author or group owner)
class MessageController {
  public:
    MessageController(services::MessageService& messages,
                      middlewares::AuthMiddleware& auth_guard) noexcept
        : messages_(messages), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

  private:
    services::MessageService& messages_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers
