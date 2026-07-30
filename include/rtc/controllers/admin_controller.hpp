#pragma once

#include <cstdint>

#include "rtc/cache/cache_service.hpp"
#include "rtc/events/event_bus.hpp"
#include "rtc/events/in_process_event_bus.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/http/app.hpp"
#include "rtc/jobs/background_executor.hpp"
#include "rtc/metrics/metrics_registry.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/realtime/connection_manager.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/services/audit_service.hpp"
#include "rtc/services/authorization_service.hpp"
#include "rtc/services/session_service.hpp"

namespace rtc::controllers {

// Administrative API.
//
// Routes (all under /api/admin, also reachable as /api/v1/admin):
//
//   Users
//     GET    /api/admin/users                    list + filter (?q=&role=&banned=)
//     GET    /api/admin/users/<id>               single user, with role and ban state
//     PUT    /api/admin/users/<id>/role          assign a role          [manage_roles]
//     POST   /api/admin/users/<id>/ban           suspend an account     [ban_users]
//     POST   /api/admin/users/<id>/unban         reinstate an account   [ban_users]
//     GET    /api/admin/users/<id>/sessions      that user's sessions   [view_sessions]
//     DELETE /api/admin/users/<id>/sessions      revoke all sessions    [revoke_sessions]
//
//   Groups
//     GET    /api/admin/conversations/<id>       conversation + participants
//     DELETE /api/admin/conversations/<id>       delete a group         [manage_groups]
//
//   Operations
//     GET    /api/admin/websockets               connected sockets, per node
//     GET    /api/admin/cache                    backend, hit ratio, counters
//     GET    /api/admin/jobs                     worker pool + event bus stats
//     GET    /api/admin/system                   version, uptime, metrics summary
//     GET    /api/admin/audit-logs               audit search           [audit.view]
//     GET    /api/admin/audit-logs/summary       audit histogram        [audit.view]
//     GET    /api/admin/features                 feature flag snapshot
//     PUT    /api/admin/features/<name>          toggle a flag          [feature_flags]
//
// Every route authenticates, then requires an explicit Permission — never a role
// comparison (see rtc/security/role.hpp for why). Mutating routes additionally
// publish an AdminAction domain event, so administrative activity lands in the
// audit log through the same path as everything else.
//
// Dependencies arrive in one struct rather than as fourteen constructor
// parameters. An administrative surface is inherently a cross-cutting view of the
// whole system, so the breadth is intrinsic; naming each member at the call site
// is what keeps the wiring readable and mis-ordering impossible.
class AdminController {
  public:
    struct Dependencies {
        middlewares::AuthMiddleware* auth_guard = nullptr;
        services::AuthorizationService* authorization = nullptr;
        services::AuditService* audit = nullptr;
        services::SessionService* sessions = nullptr;
        repositories::IUserAdminRepository* users = nullptr;
        repositories::IConversationRepository* conversations = nullptr;
        realtime::ConnectionManager* connections = nullptr;
        cache::CacheService* cache = nullptr;
        jobs::BackgroundExecutor* executor = nullptr;
        metrics::MetricsRegistry* metrics = nullptr;
        features::FeatureFlags* features = nullptr;
        events::InProcessEventBus* event_bus = nullptr;
        events::IEventPublisher* publisher = nullptr;
    };

    explicit AdminController(Dependencies dependencies) noexcept : deps_(dependencies) {}

    void register_routes(http::App& app);

  private:
    // Authenticates and enforces `permission` in one step. Returns the caller's
    // user id. Used at the top of every handler so no route can forget either half.
    [[nodiscard]] std::int64_t authorize(const crow::request& req,
                                         security::Permission permission) const;

    void register_user_routes(http::App& app);
    void register_group_routes(http::App& app);
    void register_operations_routes(http::App& app);

    Dependencies deps_;
};

}  // namespace rtc::controllers
