#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/notification_service.hpp"

namespace rtc::controllers {

// REST layer for notifications. All routes require JWT. Registers:
//
//   GET    /api/notifications            — list (?unread=true, pagination)
//   POST   /api/notifications/<id>/read  — mark one read
//   POST   /api/notifications/read-all   — mark all read
//   DELETE /api/notifications/<id>       — delete one
class NotificationController {
public:
    NotificationController(services::NotificationService& notifications,
                           middlewares::AuthMiddleware& auth_guard) noexcept
        : notifications_(notifications), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

private:
    services::NotificationService& notifications_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers
