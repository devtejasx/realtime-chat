#include "rtc/application.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <pqxx/transaction>
#include <stdexcept>
#include <utility>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/cache/redis_cache_store.hpp"
#include "rtc/database/migration_runner.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/realtime/cluster_presence.hpp"
#include "rtc/realtime/redis_cluster_bus.hpp"
#include "rtc/repositories/pg_attachment_repository.hpp"
#include "rtc/repositories/pg_audit_log_repository.hpp"
#include "rtc/repositories/pg_conversation_repository.hpp"
#include "rtc/repositories/pg_message_repository.hpp"
#include "rtc/repositories/pg_message_search_repository.hpp"
#include "rtc/repositories/pg_notification_repository.hpp"
#include "rtc/repositories/pg_reaction_repository.hpp"
#include "rtc/repositories/pg_read_receipt_repository.hpp"
#include "rtc/repositories/pg_session_repository.hpp"
#include "rtc/repositories/pg_user_admin_repository.hpp"
#include "rtc/repositories/pg_user_repository.hpp"
#include "rtc/security/bcrypt_password_hasher.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/storage/local_file_storage.hpp"
#include "rtc/tracing/otlp_http_exporter.hpp"
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

// Chooses the span exporter named by configuration. Any failure constructing a
// network exporter degrades to the logging exporter: telemetry must never be a
// reason the service refuses to start.
[[nodiscard]] std::unique_ptr<tracing::ISpanExporter> make_span_exporter(
    const config::Config& config) {
    if (config.tracing_exporter == "logging") {
        return std::make_unique<tracing::LoggingSpanExporter>();
    }
    try {
        return std::make_unique<tracing::OtlpHttpSpanExporter>(
            tracing::OtlpHttpSpanExporter::Options{
                .endpoint = config.tracing_endpoint,
                .format = tracing::parse_wire_format(config.tracing_exporter),
                .timeout = std::chrono::milliseconds{3000},
            });
    } catch (const std::exception& ex) {
        RTC_LOG_WARN("Could not create '{}' span exporter ({}); falling back to logging",
                     config.tracing_exporter,
                     ex.what());
        return std::make_unique<tracing::LoggingSpanExporter>();
    }
}

// Chooses the cluster bus: Redis Pub/Sub when multi-instance fan-out is enabled
// and compiled in, otherwise the no-op bus (which is the *correct* choice for a
// single replica, not merely a fallback).
[[nodiscard]] std::unique_ptr<realtime::IClusterBus> make_cluster_bus(const config::Config& config,
                                                                      std::string node_id) {
    if (config.cluster_enabled && realtime::RedisClusterBus::available()) {
        try {
            RTC_LOG_INFO("Cluster fan-out via Redis Pub/Sub at {}", config.redis_url);
            return std::make_unique<realtime::RedisClusterBus>(config.redis_url,
                                                               std::move(node_id));
        } catch (const std::exception& ex) {
            RTC_LOG_ERROR(
                "Cluster bus unavailable ({}); running single-instance. WebSocket delivery "
                "will be INCOMPLETE if more than one replica is serving traffic.",
                ex.what());
        }
    } else if (config.cluster_enabled) {
        RTC_LOG_ERROR(
            "CLUSTER_ENABLED is set but this binary was built without Redis support; "
            "rebuild with -DRTC_WITH_REDIS=ON. Running single-instance.");
    }
    return std::make_unique<realtime::NullClusterBus>(std::move(node_id));
}

}  // namespace

Application::Application(config::Config config)
    : config_(std::move(config)),
      migrations_dir_(utils::get_env_or("MIGRATIONS_DIR", "migrations")),
      node_id_(realtime::make_node_id()) {}

Application::~Application() {
    stop();
}

void Application::bootstrap() {
    logging::init(config_.log_level, config_.log_format);
    RTC_LOG_INFO("Starting realtime-chat {} in '{}' environment (node '{}')",
                 RTC_VERSION,
                 config_.app_env,
                 node_id_);
    RTC_LOG_INFO("Database target: {}", config_.database_connection_string_redacted());

    // Observability first: migrations and wiring are themselves worth tracing, and
    // feature flags must be readable before any component consults them.
    wire_observability();

    pool_ = std::make_unique<database::ConnectionPool>(config_.database_connection_string(),
                                                       config_.db_pool_size);
    run_migrations();
    wire_object_graph();
    wire_event_subscribers();
    register_routes();
    register_metrics();
    bootstrapped_ = true;
    RTC_LOG_INFO("Bootstrap complete");
}

void Application::wire_observability() {
    features_ = std::make_unique<features::FeatureFlags>();
    features_->load_from_env();
    RTC_LOG_INFO("Feature flags: {} of {} enabled",
                 features_->enabled_names().size(),
                 features::kFeatureCount);

    tracer_ = std::make_unique<tracing::Tracer>(
        tracing::Resource{
            .service_name = "realtime-chat",
            .service_version = RTC_VERSION,
            .deployment_environment = config_.app_env,
            .service_instance_id = node_id_,
        },
        make_span_exporter(config_),
        tracing::TracerOptions{
            .enabled = config_.tracing_enabled,
            .sample_ratio = config_.tracing_sample_ratio,
        });
    // Install as the process tracer so cross-cutting instrumentation (repository,
    // cache and WebSocket scopes) can reach it without being threaded through
    // every signature. Uninstalled in stop(), before the Tracer is destroyed.
    tracing::set_tracer(tracer_.get());
    tracer_->start();
}

void Application::wire_cluster_bus() {
    cluster_bus_ = make_cluster_bus(config_, node_id_);
    if (!cluster_bus_->is_distributed()) {
        // Single instance: local delivery and local eviction already reach
        // everything there is. Leaving the publishers unset keeps the null
        // implementations in place rather than paying for a hop to nobody.
        return;
    }

    // Registers the inbound handlers; must happen before start().
    //
    // Only the collaborators that already exist at this point are wired here.
    // Cache invalidation is deliberately *not* — AuthorizationService is
    // constructed further down wire_object_graph(), and reaching for it here
    // dereferenced a null unique_ptr and segfaulted on startup with
    // CLUSTER_ENABLED=true. wire_cluster_invalidation() therefore runs at the end
    // of the graph, once every participant is built.
    connection_manager_->set_cluster_bus(*cluster_bus_);
    wire_cluster_presence();
}

void Application::wire_cluster_presence() {
    presence_publisher_ = std::make_unique<realtime::ClusterPresencePublisher>(*cluster_bus_);
    presence_service_->set_publisher(*presence_publisher_);
    realtime::subscribe_to_presence(*cluster_bus_, *presence_service_);
}

void Application::wire_cluster_invalidation() {
    // Ordering guard. This function participates in the object graph and so
    // depends on where it is called from — a dependency the compiler cannot
    // check, and one that already went wrong once: an earlier revision invoked
    // it before AuthorizationService existed and the process died with SIGSEGV
    // immediately after logging "Cluster fan-out enabled", which reads like a
    // Redis problem and is not.
    //
    // Failing here names the actual fault instead. Refusing to start is right:
    // continuing would silently give up fleet-wide ban propagation, and a
    // security control that quietly stops working is worse than a crash.
    if (authorization_service_ == nullptr || cache_service_ == nullptr) {
        throw std::logic_error(
            "wire_cluster_invalidation() called before AuthorizationService/CacheService were "
            "constructed; it must run after the service graph is complete");
    }

    invalidation_publisher_ =
        std::make_unique<realtime::ClusterInvalidationPublisher>(*cluster_bus_);

    // Outbound: a mutation on this instance now evicts fleet-wide. Without this,
    // banning a user through one replica leaves the others honouring the cached
    // role until it expires — see AuthorizationService::invalidate.
    authorization_service_->set_invalidation_publisher(*invalidation_publisher_);
    cache_service_->set_invalidation_publisher(*invalidation_publisher_);

    // Inbound. Runs on the bus's subscriber thread, so every branch must be
    // thread-safe, and each calls the *_local variant: re-publishing on receipt
    // would amplify one eviction into one per replica, forever.
    realtime::subscribe_to_invalidations(
        *cluster_bus_, [this](const cache::InvalidationEvent& event) {
            if (event.scope == cache::invalidation_scopes::kAuthorization) {
                // Malformed ids are dropped rather than defaulted: invalidating
                // user 0 would be a silent no-op that looks like success.
                try {
                    authorization_service_->invalidate_local(std::stoll(event.key));
                } catch (const std::exception&) {
                    RTC_LOG_WARN("Ignoring authorization invalidation with unparseable id '{}'",
                                 event.key);
                }
                return;
            }
            if (event.scope == cache::invalidation_scopes::kNamespacedKey) {
                cache_service_->invalidate_local(event.key, event.sub_key);
                return;
            }
            // An unknown scope means a newer peer is publishing something this
            // build does not understand. Log once per occurrence and carry on —
            // refusing to start would turn a rolling upgrade into an outage.
            RTC_LOG_WARN("Ignoring cache invalidation with unknown scope '{}'", event.scope);
        });
}

void Application::wire_event_subscribers() {
    // Audit persistence. Its own interest check consults the audit feature flag,
    // so switching the flag off stops the writes without unsubscribing.
    audit_subscriber_ = std::make_unique<events::AuditLogSubscriber>(*audit_service_, *features_);
    event_bus_->subscribe(*audit_subscriber_);

    // A counter per domain event type. Cheap, and it makes the event bus itself
    // observable on /metrics — without it, a subscriber silently failing to fire
    // is invisible.
    metrics_subscriber_ = std::make_unique<events::FunctionSubscriber>(
        "metrics", [this](const events::DomainEvent& event) {
            metrics_->increment("rtc_domain_events_total");
            metrics_->increment("rtc_domain_event_" + std::string(event.name()) + "_total");
        });
    event_bus_->subscribe(*metrics_subscriber_);

    RTC_LOG_INFO("Domain event bus ready: {} subscriber(s), delivery={}",
                 event_bus_->subscriber_count(),
                 event_bus_->is_asynchronous() ? "asynchronous" : "synchronous");
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
    audit_log_repository_ = std::make_unique<repositories::PgAuditLogRepository>(*pool_);
    user_admin_repository_ = std::make_unique<repositories::PgUserAdminRepository>(*pool_);
    message_search_repository_ = std::make_unique<repositories::PgMessageSearchRepository>(*pool_);

    // Realtime + presence.
    connection_manager_ = std::make_unique<realtime::ConnectionManager>();
    presence_service_ = std::make_unique<services::PresenceService>();
    wire_cluster_bus();

    // Domain event bus. Asynchronous: dispatching on the worker pool keeps audit
    // writes and other subscriber side effects off the request's critical path.
    domain_event_dispatcher_ = std::make_unique<events::EventDispatcher>();
    event_bus_ = std::make_unique<events::InProcessEventBus>(*domain_event_dispatcher_, *executor_);

    // Notifications (event-driven sink + dispatcher).
    notification_service_ = std::make_unique<services::NotificationService>(
        *notification_repository_, *connection_manager_, *push_provider_, *executor_, *metrics_);
    notification_dispatcher_ =
        std::make_unique<services::NotificationDispatcher>(*notification_service_);

    // Domain services.
    user_service_ = std::make_unique<services::UserService>(*user_repository_, *password_hasher_);
    auth_service_ = std::make_unique<services::AuthService>(*user_service_, *token_service_);
    attachment_service_ = std::make_unique<services::AttachmentService>(
        *attachment_repository_,
        *message_repository_,
        *conversation_repository_,
        *file_storage_,
        *image_processor_,
        *executor_,
        *metrics_,
        services::AttachmentService::Options{config_.max_upload_bytes, 256});
    conversation_service_ =
        std::make_unique<services::ConversationService>(*conversation_repository_,
                                                        *user_repository_,
                                                        *connection_manager_,
                                                        *notification_dispatcher_);
    message_service_ = std::make_unique<services::MessageService>(*message_repository_,
                                                                  *conversation_repository_,
                                                                  *connection_manager_,
                                                                  *notification_dispatcher_,
                                                                  *attachment_service_);
    // Domain events: producers publish, subscribers react. Wiring this here (and
    // not in the service constructors) is what keeps the bus an additive change.
    conversation_service_->set_event_publisher(*event_bus_);
    message_service_->set_event_publisher(*event_bus_);
    read_receipt_service_ =
        std::make_unique<services::ReadReceiptService>(*read_receipt_repository_,
                                                       *conversation_repository_,
                                                       *message_repository_,
                                                       *connection_manager_);
    reaction_service_ = std::make_unique<services::ReactionService>(*reaction_repository_,
                                                                    *message_repository_,
                                                                    *conversation_repository_,
                                                                    *connection_manager_,
                                                                    *notification_dispatcher_);
    session_service_ = std::make_unique<services::SessionService>(
        *session_repository_, *token_service_, config_.jwt_refresh_ttl_seconds);

    // Authorisation, audit and search.
    authorization_service_ = std::make_unique<services::AuthorizationService>(
        *user_admin_repository_,
        *cache_store_,
        services::AuthorizationService::Options{
            std::chrono::seconds(config_.authz_cache_ttl_seconds)});
    audit_service_ =
        std::make_unique<services::AuditService>(*audit_log_repository_, *user_repository_);
    search_service_ =
        std::make_unique<services::SearchService>(*message_search_repository_, *features_);

    // Safe only here: both participants now exist. Guarded on the bus being
    // distributed so a single instance keeps its null publishers.
    if (cluster_bus_ != nullptr && cluster_bus_->is_distributed()) {
        wire_cluster_invalidation();
    }

    auth_guard_ = std::make_unique<middlewares::AuthMiddleware>(*token_service_);
    // Enables account-suspension enforcement on every authenticated endpoint, so a
    // ban takes effect on the target's next request rather than at token expiry.
    auth_guard_->set_authorization_service(*authorization_service_);

    event_dispatcher_ = std::make_unique<realtime::EventDispatcher>(*connection_manager_,
                                                                    *presence_service_,
                                                                    *conversation_service_,
                                                                    *message_service_,
                                                                    *read_receipt_service_);
    event_dispatcher_->set_feature_flags(*features_);
    event_dispatcher_->set_event_publisher(*event_bus_);

    heartbeat_monitor_ = std::make_unique<realtime::HeartbeatMonitor>(
        *connection_manager_,
        std::chrono::seconds(config_.ws_heartbeat_interval_seconds),
        std::chrono::seconds(config_.ws_heartbeat_timeout_seconds));

    // Controllers.
    health_controller_ = std::make_unique<controllers::HealthController>(config_);
    health_controller_->set_readiness_dependencies(*pool_, *cache_store_);
    health_controller_->set_worker_dependencies(*executor_, *scheduler_);
    health_controller_->set_cluster_bus(*cluster_bus_);
    auth_controller_ = std::make_unique<controllers::AuthController>(
        *auth_service_, *user_service_, *session_service_, *auth_guard_);
    auth_controller_->set_event_publisher(*event_bus_);
    user_controller_ = std::make_unique<controllers::UserController>(*user_service_, *auth_guard_);
    conversation_controller_ =
        std::make_unique<controllers::ConversationController>(*conversation_service_, *auth_guard_);
    message_controller_ =
        std::make_unique<controllers::MessageController>(*message_service_, *auth_guard_);
    websocket_controller_ =
        std::make_unique<controllers::WebSocketController>(*token_service_, *event_dispatcher_);
    attachment_controller_ = std::make_unique<controllers::AttachmentController>(
        *attachment_service_, *auth_guard_, *rate_limiter_, config_);
    reaction_controller_ =
        std::make_unique<controllers::ReactionController>(*reaction_service_, *auth_guard_);
    notification_controller_ =
        std::make_unique<controllers::NotificationController>(*notification_service_, *auth_guard_);
    session_controller_ =
        std::make_unique<controllers::SessionController>(*session_service_, *auth_guard_);
    metrics_controller_ = std::make_unique<controllers::MetricsController>(*metrics_);
    search_controller_ =
        std::make_unique<controllers::SearchController>(*search_service_, *auth_guard_);
    docs_controller_ = std::make_unique<controllers::DocsController>(config_);
    admin_controller_ =
        std::make_unique<controllers::AdminController>(controllers::AdminController::Dependencies{
            .auth_guard = auth_guard_.get(),
            .authorization = authorization_service_.get(),
            .audit = audit_service_.get(),
            .sessions = session_service_.get(),
            .users = user_admin_repository_.get(),
            .conversations = conversation_repository_.get(),
            .connections = connection_manager_.get(),
            .cache = cache_service_.get(),
            .executor = executor_.get(),
            .metrics = metrics_.get(),
            .features = features_.get(),
            .event_bus = event_bus_.get(),
            .publisher = event_bus_.get(),
        });

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
    metrics_->register_gauge_callback("rtc_background_jobs_pending",
                                      [this] { return static_cast<double>(executor_->pending()); });

    // --- Phase 5 instrumentation ---
    metrics_->register_gauge_callback("rtc_event_bus_published_total", [this] {
        return static_cast<double>(event_bus_->published_count());
    });
    metrics_->register_gauge_callback("rtc_event_bus_dropped_total", [this] {
        return static_cast<double>(event_bus_->dropped_count());
    });
    metrics_->register_gauge_callback("rtc_event_bus_handler_failures_total", [this] {
        return static_cast<double>(domain_event_dispatcher_->handler_failure_count());
    });
    metrics_->register_gauge_callback("rtc_authz_cache_hits_total", [this] {
        return static_cast<double>(authorization_service_->cache_hits());
    });
    metrics_->register_gauge_callback("rtc_authz_cache_misses_total", [this] {
        return static_cast<double>(authorization_service_->cache_misses());
    });
    // Tracing self-observability: a rising drop count means the collector cannot
    // keep up, which is the signal to lower the sample ratio.
    metrics_->register_gauge_callback("rtc_spans_started_total", [this] {
        return static_cast<double>(tracer_->spans_started());
    });
    metrics_->register_gauge_callback("rtc_spans_exported_total", [this] {
        return static_cast<double>(tracer_->spans_exported());
    });
    metrics_->register_gauge_callback("rtc_spans_dropped_total", [this] {
        return static_cast<double>(tracer_->spans_dropped());
    });
    metrics_->register_gauge_callback("rtc_cluster_published_total", [this] {
        return static_cast<double>(cluster_bus_->published_count());
    });
    metrics_->register_gauge_callback("rtc_cluster_received_total", [this] {
        return static_cast<double>(cluster_bus_->received_count());
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
    search_controller_->register_routes(*app_);
    admin_controller_->register_routes(*app_);
    docs_controller_->register_routes(*app_);

    // Must stay last. Crow resolves a path by keeping the *lowest* matching rule
    // index, so the versioned catch-all only wins for requests no concrete route
    // above claimed. Registering it earlier would shadow the entire API.
    controllers::ApiFallbackController::register_routes(*app_);
}

int Application::run() {
    if (!bootstrapped_) {
        bootstrap();
    }
    executor_->start();
    scheduler_->start();
    heartbeat_monitor_->start();
    cluster_bus_->start();
    // Only now is the instance genuinely able to serve: migrations are applied and
    // every background thread is live. /health/startup flips to 200 here, which is
    // what releases the Kubernetes startupProbe.
    health_controller_->mark_started();

    RTC_LOG_INFO("HTTP + WebSocket server listening on port {}", config_.chat_port);
    RTC_LOG_INFO("API: /api/v1 (legacy /api supported) — docs at /docs, spec at /openapi.json");
    app_->loglevel(crow::LogLevel::Warning);
    app_->port(config_.chat_port).multithreaded().run();
    RTC_LOG_INFO("HTTP server stopped");

    // Reverse order of startup. The cluster bus stops first so no inbound message
    // can reach connections that are about to go away.
    cluster_bus_->stop();
    heartbeat_monitor_->stop();
    scheduler_->stop();
    executor_->stop();
    tracing::set_tracer(nullptr);
    tracer_->stop();
    logging::shutdown();
    return 0;
}

int Application::migrate() {
    logging::init(config_.log_level, config_.log_format);
    RTC_LOG_INFO("Running migrations only for realtime-chat {}", RTC_VERSION);
    pool_ = std::make_unique<database::ConnectionPool>(config_.database_connection_string(),
                                                       config_.db_pool_size);
    run_migrations();
    RTC_LOG_INFO("Migrations complete; exiting");
    logging::shutdown();
    return 0;
}

void Application::stop() {
    // Order matters and mirrors startup in reverse. Every step is null- and
    // idempotency-guarded because stop() is reachable from a signal handler, from
    // run()'s normal exit, and from the destructor.
    if (cluster_bus_) {
        cluster_bus_->stop();
    }
    if (heartbeat_monitor_) {
        heartbeat_monitor_->stop();
    }
    if (scheduler_) {
        scheduler_->stop();
    }
    if (executor_) {
        // Must stop before the object graph unwinds: in-flight event-bus tasks hold
        // references to services, so a task still running during destruction would
        // touch freed memory.
        executor_->stop();
    }
    if (tracer_) {
        // Uninstall before destroying, so nothing can reach a dangling tracer.
        tracing::set_tracer(nullptr);
        tracer_->stop();
    }
    if (app_) {
        RTC_LOG_INFO("Graceful shutdown requested");
        app_->stop();
    }
}

}  // namespace rtc
