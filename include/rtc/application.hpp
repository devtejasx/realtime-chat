#pragma once

#include <memory>
#include <string>

#include "rtc/cache/cache_service.hpp"
#include "rtc/cache/cache_store.hpp"
#include "rtc/cache/presence_cache.hpp"
#include "rtc/config/config.hpp"
#include "rtc/controllers/admin_controller.hpp"
#include "rtc/controllers/api_fallback_controller.hpp"
#include "rtc/controllers/attachment_controller.hpp"
#include "rtc/controllers/auth_controller.hpp"
#include "rtc/controllers/conversation_controller.hpp"
#include "rtc/controllers/docs_controller.hpp"
#include "rtc/controllers/health_controller.hpp"
#include "rtc/controllers/message_controller.hpp"
#include "rtc/controllers/metrics_controller.hpp"
#include "rtc/controllers/notification_controller.hpp"
#include "rtc/controllers/reaction_controller.hpp"
#include "rtc/controllers/search_controller.hpp"
#include "rtc/controllers/session_controller.hpp"
#include "rtc/controllers/user_controller.hpp"
#include "rtc/controllers/websocket_controller.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/events/audit_log_subscriber.hpp"
#include "rtc/events/event_dispatcher.hpp"
#include "rtc/events/function_subscriber.hpp"
#include "rtc/events/in_process_event_bus.hpp"
#include "rtc/features/feature_flags.hpp"
#include "rtc/http/app.hpp"
#include "rtc/jobs/background_executor.hpp"
#include "rtc/jobs/periodic_scheduler.hpp"
#include "rtc/media/image_processor.hpp"
#include "rtc/metrics/metrics_registry.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/notifications/push_provider.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"
#include "rtc/realtime/cluster_bus.hpp"
#include "rtc/realtime/cluster_invalidation.hpp"
#include "rtc/realtime/cluster_presence.hpp"
#include "rtc/realtime/connection_manager.hpp"
#include "rtc/realtime/event_dispatcher.hpp"
#include "rtc/realtime/heartbeat_monitor.hpp"
#include "rtc/repositories/attachment_repository.hpp"
#include "rtc/repositories/audit_log_repository.hpp"
#include "rtc/repositories/conversation_repository.hpp"
#include "rtc/repositories/message_repository.hpp"
#include "rtc/repositories/message_search_repository.hpp"
#include "rtc/repositories/notification_repository.hpp"
#include "rtc/repositories/reaction_repository.hpp"
#include "rtc/repositories/read_receipt_repository.hpp"
#include "rtc/repositories/session_repository.hpp"
#include "rtc/repositories/user_admin_repository.hpp"
#include "rtc/repositories/user_repository.hpp"
#include "rtc/security/password_hasher.hpp"
#include "rtc/security/token_service.hpp"
#include "rtc/services/attachment_service.hpp"
#include "rtc/services/audit_service.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/authorization_service.hpp"
#include "rtc/services/conversation_service.hpp"
#include "rtc/services/message_service.hpp"
#include "rtc/services/notification_dispatcher.hpp"
#include "rtc/services/notification_service.hpp"
#include "rtc/services/presence_service.hpp"
#include "rtc/services/reaction_service.hpp"
#include "rtc/services/read_receipt_service.hpp"
#include "rtc/services/search_service.hpp"
#include "rtc/services/session_service.hpp"
#include "rtc/services/user_service.hpp"
#include "rtc/storage/file_storage.hpp"
#include "rtc/tracing/tracer.hpp"

namespace rtc {

// Application composition root.
//
// Owns the entire object graph and wires concrete implementations to their
// interfaces in exactly one place (dependency injection by construction). No
// other component constructs its collaborators, which keeps the layers
// decoupled and unit-testable. Lifecycle: construct -> bootstrap() -> run(),
// with stop() available for graceful shutdown from a signal handler.
class Application {
  public:
    explicit Application(config::Config config);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Initialises logging, the connection pool, runs migrations, constructs the
    // object graph and registers routes. Throws on any fatal startup error.
    void bootstrap();

    // Starts the HTTP server. Blocks until stop() is called or the process is
    // signalled. Returns the process exit code.
    [[nodiscard]] int run();

    // Runs pending migrations and exits (no server). Used by deploy pipelines to
    // apply the schema ahead of a rolling release. Idempotent. Returns the exit
    // code.
    [[nodiscard]] int migrate();

    // Requests a graceful shutdown of the running server. Safe to call from a
    // signal handler context.
    void stop();

    [[nodiscard]] const config::Config& config() const noexcept { return config_; }

  private:
    void run_migrations();
    void wire_object_graph();
    void register_routes();

    void register_metrics();

    // Phase 5 wiring, split out of wire_object_graph() to keep each step legible.
    void wire_observability();         // tracer + feature flags (before anything traced)
    void wire_cluster_bus();           // cross-instance fan-out
    void wire_cluster_invalidation();  // cross-instance cache coherence
    void wire_cluster_presence();      // cross-instance online/offline view
    void wire_event_subscribers();     // domain event bus consumers

    config::Config config_;
    std::string migrations_dir_;

    // --- Declaration order is destruction order (reversed) ---
    //
    // Members are destroyed bottom-to-top, so anything that other components hold
    // a reference to must be declared *above* them. Two orderings here are load-
    // bearing:
    //
    //   * features_ and tracer_ are declared first: the tracer is installed as the
    //     process-wide provider and is referenced from every layer, and the flags
    //     are read by services, controllers and the WebSocket dispatcher.
    //   * event_bus_ dispatches onto executor_, and its subscribers reference
    //     services. stop() halts the executor before any destruction begins, so no
    //     task can be in flight while the graph unwinds.

    // Runtime feature flags and tracing. Wired first, used by everything after.
    std::unique_ptr<features::FeatureFlags> features_;
    std::unique_ptr<tracing::Tracer> tracer_;

    std::unique_ptr<database::ConnectionPool> pool_;
    std::unique_ptr<security::IPasswordHasher> password_hasher_;
    std::unique_ptr<security::ITokenService> token_service_;

    // Phase 3 infrastructure: cache, metrics, background work, storage.
    std::unique_ptr<metrics::MetricsRegistry> metrics_;
    std::unique_ptr<cache::ICacheStore> cache_store_;
    std::unique_ptr<cache::CacheService> cache_service_;
    std::unique_ptr<cache::PresenceCache> presence_cache_;
    std::unique_ptr<ratelimit::RateLimiter> rate_limiter_;
    std::unique_ptr<jobs::BackgroundExecutor> executor_;
    std::unique_ptr<jobs::PeriodicScheduler> scheduler_;
    std::unique_ptr<storage::IFileStorage> file_storage_;
    std::unique_ptr<media::IImageProcessor> image_processor_;
    std::unique_ptr<notifications::IPushProvider> push_provider_;

    // Repositories
    std::unique_ptr<repositories::IUserRepository> user_repository_;
    std::unique_ptr<repositories::IConversationRepository> conversation_repository_;
    std::unique_ptr<repositories::IMessageRepository> message_repository_;
    std::unique_ptr<repositories::IReadReceiptRepository> read_receipt_repository_;
    std::unique_ptr<repositories::IAttachmentRepository> attachment_repository_;
    std::unique_ptr<repositories::IReactionRepository> reaction_repository_;
    std::unique_ptr<repositories::INotificationRepository> notification_repository_;
    std::unique_ptr<repositories::ISessionRepository> session_repository_;
    std::unique_ptr<repositories::IAuditLogRepository> audit_log_repository_;
    std::unique_ptr<repositories::IUserAdminRepository> user_admin_repository_;
    std::unique_ptr<repositories::IMessageSearchRepository> message_search_repository_;

    // Cross-instance fan-out. Declared before the connection manager, which holds
    // a non-owning pointer to it.
    std::unique_ptr<realtime::IClusterBus> cluster_bus_;
    std::unique_ptr<realtime::ClusterInvalidationPublisher> invalidation_publisher_;
    std::unique_ptr<realtime::ClusterPresencePublisher> presence_publisher_;

    // Realtime infrastructure (ConnectionManager is the IEventBroadcaster).
    std::unique_ptr<realtime::ConnectionManager> connection_manager_;
    std::unique_ptr<services::PresenceService> presence_service_;

    // Services
    std::unique_ptr<services::NotificationService> notification_service_;
    std::unique_ptr<services::NotificationDispatcher> notification_dispatcher_;
    std::unique_ptr<services::UserService> user_service_;
    std::unique_ptr<services::AuthService> auth_service_;
    std::unique_ptr<services::AttachmentService> attachment_service_;
    std::unique_ptr<services::ConversationService> conversation_service_;
    std::unique_ptr<services::MessageService> message_service_;
    std::unique_ptr<services::ReadReceiptService> read_receipt_service_;
    std::unique_ptr<services::ReactionService> reaction_service_;
    std::unique_ptr<services::SessionService> session_service_;
    std::unique_ptr<services::AuthorizationService> authorization_service_;
    std::unique_ptr<services::AuditService> audit_service_;
    std::unique_ptr<services::SearchService> search_service_;

    // Domain event bus. The dispatcher owns the subscriber registry, so it is
    // declared before the bus and before the subscribers it routes to.
    std::unique_ptr<events::EventDispatcher> domain_event_dispatcher_;
    std::unique_ptr<events::InProcessEventBus> event_bus_;
    std::unique_ptr<events::AuditLogSubscriber> audit_subscriber_;
    std::unique_ptr<events::FunctionSubscriber> metrics_subscriber_;

    std::unique_ptr<middlewares::AuthMiddleware> auth_guard_;
    std::unique_ptr<realtime::EventDispatcher> event_dispatcher_;
    std::unique_ptr<realtime::HeartbeatMonitor> heartbeat_monitor_;

    // Controllers
    std::unique_ptr<controllers::HealthController> health_controller_;
    std::unique_ptr<controllers::AuthController> auth_controller_;
    std::unique_ptr<controllers::UserController> user_controller_;
    std::unique_ptr<controllers::ConversationController> conversation_controller_;
    std::unique_ptr<controllers::MessageController> message_controller_;
    std::unique_ptr<controllers::WebSocketController> websocket_controller_;
    std::unique_ptr<controllers::AttachmentController> attachment_controller_;
    std::unique_ptr<controllers::ReactionController> reaction_controller_;
    std::unique_ptr<controllers::NotificationController> notification_controller_;
    std::unique_ptr<controllers::SessionController> session_controller_;
    std::unique_ptr<controllers::MetricsController> metrics_controller_;
    std::unique_ptr<controllers::SearchController> search_controller_;
    std::unique_ptr<controllers::AdminController> admin_controller_;
    std::unique_ptr<controllers::DocsController> docs_controller_;

    std::unique_ptr<http::App> app_;
    std::string node_id_;
    bool bootstrapped_ = false;
};

}  // namespace rtc
