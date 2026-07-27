#include "rtc/application.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <utility>

#include <pqxx/transaction>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/cache/redis_cache_store.hpp"
#include "rtc/database/migration_runner.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/repositories/pg_attachment_repository.hpp"
#include "rtc/repositories/pg_conversation_repository.hpp"
#include "rtc/repositories/pg_message_repository.hpp"
#include "rtc/repositories/pg_notification_repository.hpp"
#include "rtc/repositories/pg_reaction_repository.hpp"
#include "rtc/repositories/pg_read_receipt_repository.hpp"
#include "rtc/repositories/pg_session_repository.hpp"
#include "rtc/repositories/pg_user_repository.hpp"
#include "rtc/security/bcrypt_password_hasher.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/storage/local_file_storage.hpp"
#include "rtc/utils/env.hpp"

namespace rtc {
namespace {

// Resident memory in bytes (Linux); 0 where unavailable. Surfaced as a metric.
[[nodiscard]] double process_memory_bytes() {
#if defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
        long pages = 0;
        long resident = 0;
        if (std::fscanf(f, "%ld %ld", &pages, &resident) == 2) {
            std::fclose(f);
            return static_cast<double>(resident) * 4096.0;  // page size
        }
        std::fclose(f);
    }
#endif
    return 0.0;
}

// Chooses the cache backend: Redis when enabled and compiled in, else the
// in-memory store. Any Redis connection failure falls back gracefully.
[[nodiscard]] std::unique_ptr<cache::ICacheStore> make_cache_store(const config::Config& config) {
    if (config.redis_enabled && cache::RedisCacheStore::available()) {
        try {
            RTC_LOG_INFO("Using Redis cache backend at {}", config.redis_url);
            return std::make_unique<cache::RedisCacheStore>(config.redis_url);
        } catch (const std::exception& ex) {
            RTC_LOG_WARN("Redis unavailable ({}); falling back to in-memory cache", ex.what());
        }
    }
    RTC_LOG_INFO("Using in-memory cache backend");
    return std::make_unique<cache::InMemoryCacheStore>();
}

}  // namespace

Application::Application(config::Config config)
    : config_(std::move(config)),
      migrations_dir_(utils::get_env_or("MIGRATIONS_DIR", "migrations")) {}

Application::~Application() { stop(); }

void Application::bootstrap() {
    logging::init(config_.log_level, config_.log_format);
    RTC_LOG_INFO("Starting realtime-chat {} in '{}' environment", RTC_VERSION, config_.app_env);
    RTC_LOG_INFO("Database target: {}", config_.database_connection_string_redacted());

    pool_ = std::make_unique<database::ConnectionPool>(config_.database_connection_string(),
                                                       config_.db_pool_size);
    run_migrations();
    wire_object_graph();
    register_routes();
    register_metrics();
    bootstrapped_ = true;
    RTC_LOG_INFO("Bootstrap complete");
}

void Application::run_migrations() {
    const std::filesystem::path dir = migrations_dir_;
    RTC_LOG_INFO("Running migrations from '{}'", dir.string());
    database::MigrationRunner runner(*pool_, dir);
    runner.run();
}

void Application::wire_object_graph() {
    password_hasher_ = std::make_unique<security::BcryptPasswordHasher>();
    token_service_ = std::make_unique<security::JwtTokenService>(security::JwtTokenService::Options{
        .secret = config_.jwt_secret,
        .issuer = config_.jwt_issuer,
        .access_ttl_seconds = config_.jwt_access_ttl_seconds,
        .refresh_ttl_seconds = config_.jwt_refresh_ttl_seconds,
    });

    // Phase 3 infrastructure.
    metrics_ = std::make_unique<metrics::MetricsRegistry>();
    cache_store_ = make_cache_store(config_);
    cache_service_ = std::make_unique<cache::CacheService>(*cache_store_);
    presence_cache_ = std::make_unique<cache::PresenceCache>(*cache_store_);
    rate_limiter_ =
        std::make_unique<ratelimit::RateLimiter>(*cache_store_, config_.rate_limit_enabled);
    executor_ = std::make_unique<jobs::BackgroundExecutor>(
        static_cast<std::size_t>(config_.worker_threads));
    scheduler_ = std::make_unique<jobs::PeriodicScheduler>(
        std::chrono::seconds(config_.maintenance_interval_seconds));
    file_storage_ = std::make_unique<storage::LocalFileStorage>(config_.upload_dir);
    image_processor_ = std::make_unique<media::NoopImageProcessor>();
    push_provider_ = std::make_unique<notifications::NullPushProvider>();

    // Repositories.
    user_repository_ = std::make_unique<repositories::PgUserRepository>(*pool_);
    conversation_repository_ = std::make_unique<repositories::PgConversationRepository>(*pool_);
    message_repository_ = std::make_unique<repositories::PgMessageRepository>(*pool_);
    read_receipt_repository_ = std::make_unique<repositories::PgReadReceiptRepository>(*pool_);
    attachment_repository_ = std::make_unique<repositories::PgAttachmentRepository>(*pool_);
    reaction_repository_ = std::make_unique<repositories::PgReactionRepository>(*pool_);
    notification_repository_ = std::make_unique<repositories::PgNotificationRepository>(*pool_);
    session_repository_ = std::make_unique<repositories::PgSessionRepository>(*pool_);

    // Realtime + presence.
    connection_manager_ = std::make_unique<realtime::ConnectionManager>();
    presence_service_ = std::make_unique<services::PresenceService>();

    // Notifications (event-driven sink + dispatcher).
    notification_service_ = std::make_unique<services::NotificationService>(
        *notification_repository_, *connection_manager_, *push_provider_, *executor_, *metrics_);
    notification_dispatcher_ =
        std::make_unique<services::NotificationDispatcher>(*notification_service_);

    // Domain services.
    user_service_ = std::make_unique<services::UserService>(*user_repository_, *password_hasher_);
    auth_service_ = std::make_unique<services::AuthService>(*user_service_, *token_service_);
    attachment_service_ = std::make_unique<services::AttachmentService>(
        *attachment_repository_, *message_repository_, *conversation_repository_, *file_storage_,
        *image_processor_, *executor_, *metrics_,
        services::AttachmentService::Options{config_.max_upload_bytes, 256});
    conversation_service_ = std::make_unique<services::ConversationService>(
        *conversation_repository_, *user_repository_, *connection_manager_,
        *notification_dispatcher_);
    message_service_ = std::make_unique<services::MessageService>(
        *message_repository_, *conversation_repository_, *connection_manager_,
        *notification_dispatcher_, *attachment_service_);
    read_receipt_service_ = std::make_unique<services::ReadReceiptService>(
        *read_receipt_repository_, *conversation_repository_, *message_repository_,
        *connection_manager_);
    reaction_service_ = std::make_unique<services::ReactionService>(
        *reaction_repository_, *message_repository_, *conversation_repository_,
        *connection_manager_, *notification_dispatcher_);
    session_service_ = std::make_unique<services::SessionService>(
        *session_repository_, *token_service_, config_.jwt_refresh_ttl_seconds);

    auth_guard_ = std::make_unique<middlewares::AuthMiddleware>(*token_service_);
    event_dispatcher_ = std::make_unique<realtime::EventDispatcher>(
        *connection_manager_, *presence_service_, *conversation_service_, *message_service_,
        *read_receipt_service_);
    heartbeat_monitor_ = std::make_unique<realtime::HeartbeatMonitor>(
        *connection_manager_, std::chrono::seconds(config_.ws_heartbeat_interval_seconds),
        std::chrono::seconds(config_.ws_heartbeat_timeout_seconds));

    // Controllers.
    health_controller_ = std::make_unique<controllers::HealthController>(config_);
    health_controller_->set_readiness_dependencies(*pool_, *cache_store_);
    auth_controller_ = std::make_unique<controllers::AuthController>(
        *auth_service_, *user_service_, *session_service_, *auth_guard_);
    user_controller_ =
        std::make_unique<controllers::UserController>(*user_service_, *auth_guard_);
    conversation_controller_ =
        std::make_unique<controllers::ConversationController>(*conversation_service_, *auth_guard_);
    message_controller_ =
        std::make_unique<controllers::MessageController>(*message_service_, *auth_guard_);
    websocket_controller_ = std::make_unique<controllers::WebSocketController>(*token_service_,
                                                                               *event_dispatcher_);
    attachment_controller_ = std::make_unique<controllers::AttachmentController>(
        *attachment_service_, *auth_guard_, *rate_limiter_, config_);
    reaction_controller_ =
        std::make_unique<controllers::ReactionController>(*reaction_service_, *auth_guard_);
    notification_controller_ =
        std::make_unique<controllers::NotificationController>(*notification_service_, *auth_guard_);
    session_controller_ =
        std::make_unique<controllers::SessionController>(*session_service_, *auth_guard_);
    metrics_controller_ = std::make_unique<controllers::MetricsController>(*metrics_);

    app_ = std::make_unique<http::App>();
    app_->get_middleware<middlewares::SecurityMiddleware>().set_allowed_origins(
        config_.cors_allowed_origins);
    app_->get_middleware<middlewares::MetricsMiddleware>().set_registry(metrics_.get());

    // Maintenance jobs.
    scheduler_->add("cache_purge", [this] { cache_store_->purge_expired(); });
    scheduler_->add("session_cleanup", [this] {
        const auto removed = session_service_->cleanup_expired();
        if (removed > 0) {
            RTC_LOG_DEBUG("Cleaned up {} expired session(s)", removed);
        }
    });
    // Dependency-latency probes surfaced as gauges on /metrics.
    scheduler_->add("latency_probe", [this] {
        using Clock = std::chrono::steady_clock;
        try {
            const auto t0 = Clock::now();
            {
                auto lease = pool_->acquire();
                pqxx::work txn(lease.get());
                txn.exec("SELECT 1");
                txn.commit();
            }
            metrics_->set_gauge("rtc_db_query_seconds",
                                std::chrono::duration<double>(Clock::now() - t0).count());
        } catch (const std::exception&) {
            metrics_->set_gauge("rtc_db_query_seconds", -1.0);  // probe failed
        }
        try {
            const auto t0 = Clock::now();
            cache_store_->set("health:probe", "1", std::chrono::seconds(30));
            (void) cache_store_->get("health:probe");
            metrics_->set_gauge("rtc_cache_op_seconds",
                                std::chrono::duration<double>(Clock::now() - t0).count());
        } catch (const std::exception&) {
            metrics_->set_gauge("rtc_cache_op_seconds", -1.0);
        }
    });
}

void Application::register_metrics() {
    metrics_->register_gauge_callback("rtc_ws_connections", [this] {
        return static_cast<double>(connection_manager_->sessions().session_count());
    });
    metrics_->register_gauge_callback("rtc_active_users", [this] {
        return static_cast<double>(presence_service_->online_count());
    });
    metrics_->register_gauge_callback("rtc_cache_hit_ratio",
                                      [this] { return cache_service_->hit_ratio(); });
    metrics_->register_gauge_callback("rtc_uptime_seconds",
                                      [this] { return metrics_->uptime_seconds(); });
    metrics_->register_gauge_callback("rtc_process_memory_bytes",
                                      [] { return process_memory_bytes(); });
    metrics_->register_gauge_callback("rtc_background_jobs_pending", [this] {
        return static_cast<double>(executor_->pending());
    });
}

void Application::register_routes() {
    health_controller_->register_routes(*app_);
    auth_controller_->register_routes(*app_);
    user_controller_->register_routes(*app_);
    conversation_controller_->register_routes(*app_);
    message_controller_->register_routes(*app_);
    websocket_controller_->register_routes(*app_);
    attachment_controller_->register_routes(*app_);
    reaction_controller_->register_routes(*app_);
    notification_controller_->register_routes(*app_);
    session_controller_->register_routes(*app_);
    metrics_controller_->register_routes(*app_);
}

int Application::run() {
    if (!bootstrapped_) {
        bootstrap();
    }
    executor_->start();
    scheduler_->start();
    heartbeat_monitor_->start();
    RTC_LOG_INFO("HTTP + WebSocket server listening on port {}", config_.chat_port);
    app_->loglevel(crow::LogLevel::Warning);
    app_->port(config_.chat_port).multithreaded().run();
    RTC_LOG_INFO("HTTP server stopped");
    heartbeat_monitor_->stop();
    scheduler_->stop();
    executor_->stop();
    logging::shutdown();
    return 0;
}

void Application::stop() {
    if (heartbeat_monitor_) {
        heartbeat_monitor_->stop();
    }
    if (scheduler_) {
        scheduler_->stop();
    }
    if (executor_) {
        executor_->stop();
    }
    if (app_) {
        RTC_LOG_INFO("Graceful shutdown requested");
        app_->stop();
    }
}

}  // namespace rtc
