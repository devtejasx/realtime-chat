# ---------------------------------------------------------------------------
# rtc_password_hashing — bcrypt isolation boundary.
#
# Bcrypt.cpp publishes its API as <bcrypt.h>, which collides with the Windows
# SDK header of the same name (the BCrypt CNG API) that <wincrypt.h> — pulled in
# by OpenSSL, and therefore by jwt-cpp — includes. If Bcrypt.cpp's include
# directory is on the search path for the whole library, every OpenSSL-using
# translation unit resolves <bcrypt.h> to the wrong header and fails to compile.
#
# Exactly one translation unit needs that header, so it gets its own target and
# the dependency stays PRIVATE to it. Beyond unbreaking Windows builds this is
# simply better include hygiene: a third-party header's namespace no longer
# leaks across ~70 unrelated translation units.
# ---------------------------------------------------------------------------
add_library(rtc_password_hashing STATIC security/bcrypt_password_hasher.cpp)
target_include_directories(rtc_password_hashing PUBLIC "${CMAKE_SOURCE_DIR}/include")
# OpenSSL::Crypto supplies RAND_bytes. Bcrypt.cpp's own salt generation falls back
# to srand(time(NULL)) + rand() off OpenBSD, which makes the salt a function of the
# current second — see the comment block in bcrypt_password_hasher.cpp.
target_link_libraries(rtc_password_hashing PRIVATE bcrypt OpenSSL::Crypto rtc_warnings)

# ---------------------------------------------------------------------------
# rtc_core — the application library.
#
# Contains every layer (config, logging, errors, database, security, domain,
# services, controllers, middlewares, composition root). The server executable
# and the test suite both link against it, so business logic is compiled once
# and exercised directly by tests.
# ---------------------------------------------------------------------------
add_library(rtc_core STATIC
    config/config.cpp
    logging/logger.cpp
    logging/log_context.cpp
    errors/error_mapper.cpp
    utils/time.cpp
    security/jwt_token_service.cpp
    utils/hash.cpp
    dto/auth_dto.cpp
    dto/profile_dto.cpp
    dto/pagination.cpp
    dto/conversation_dto.cpp
    dto/message_dto.cpp
    dto/read_receipt_dto.cpp
    validation/validators.cpp
    database/connection_pool.cpp
    database/migration_runner.cpp
    repositories/pg_user_repository.cpp
    repositories/pg_conversation_repository.cpp
    repositories/pg_message_repository.cpp
    repositories/pg_read_receipt_repository.cpp
    repositories/pg_attachment_repository.cpp
    repositories/pg_reaction_repository.cpp
    repositories/pg_notification_repository.cpp
    repositories/pg_session_repository.cpp
    repositories/pg_audit_log_repository.cpp
    repositories/pg_user_admin_repository.cpp
    repositories/pg_message_search_repository.cpp
    services/authorization_service.cpp
    services/audit_service.cpp
    services/search_service.cpp
    services/user_service.cpp
    services/auth_service.cpp
    services/conversation_service.cpp
    services/message_service.cpp
    services/read_receipt_service.cpp
    services/presence_service.cpp
    services/attachment_service.cpp
    services/reaction_service.cpp
    services/notification_service.cpp
    services/notification_dispatcher.cpp
    services/session_service.cpp
    cache/in_memory_cache_store.cpp
    cache/redis_cache_store.cpp
    cache/cache_service.cpp
    cache/presence_cache.cpp
    ratelimit/rate_limiter.cpp
    jobs/background_executor.cpp
    jobs/periodic_scheduler.cpp
    metrics/metrics_registry.cpp
    messaging/in_memory_broker.cpp
    reliability/retry_policy.cpp
    reliability/circuit_breaker.cpp
    media/mime.cpp
    storage/local_file_storage.cpp
    realtime/protocol.cpp
    realtime/cluster_bus.cpp
    realtime/redis_cluster_bus.cpp
    realtime/session_manager.cpp
    realtime/room_manager.cpp
    realtime/connection_manager.cpp
    realtime/event_dispatcher.cpp
    realtime/heartbeat_monitor.cpp
    middlewares/auth_middleware.cpp
    middlewares/versioning_middleware.cpp
    middlewares/tracing_middleware.cpp
    features/feature_flags.cpp
    events/domain_event.cpp
    events/event_dispatcher.cpp
    events/in_process_event_bus.cpp
    tracing/trace_context.cpp
    tracing/span.cpp
    tracing/tracer.cpp
    tracing/span_exporter.cpp
    tracing/otlp_http_exporter.cpp
    tracing/scoped_span.cpp
    controllers/health_controller.cpp
    controllers/auth_controller.cpp
    controllers/user_controller.cpp
    controllers/conversation_controller.cpp
    controllers/message_controller.cpp
    controllers/websocket_controller.cpp
    controllers/attachment_controller.cpp
    controllers/reaction_controller.cpp
    controllers/notification_controller.cpp
    controllers/session_controller.cpp
    controllers/metrics_controller.cpp
    controllers/search_controller.cpp
    controllers/admin_controller.cpp
    controllers/docs_controller.cpp
    controllers/api_fallback_controller.cpp
    docs/openapi_spec.cpp
    application.cpp
)

# Optional Redis backend (redis-plus-plus). Off by default so the project builds
# with no external cache dependency; the in-memory store is used instead.
if(RTC_WITH_REDIS AND TARGET redis++::redis++_static)
    target_compile_definitions(rtc_core PRIVATE RTC_WITH_REDIS)
    target_link_libraries(rtc_core PRIVATE redis++::redis++_static)
elseif(RTC_WITH_REDIS AND TARGET redis++::redis++)
    target_compile_definitions(rtc_core PRIVATE RTC_WITH_REDIS)
    target_link_libraries(rtc_core PRIVATE redis++::redis++)
endif()

target_include_directories(rtc_core PUBLIC "${CMAKE_SOURCE_DIR}/include")

target_link_libraries(rtc_core
    PUBLIC
        ${RTC_CROW_TARGET}
        asio
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ${RTC_JWT_TARGET}
        ${RTC_PQXX_TARGET}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
    PRIVATE
        rtc_password_hashing
        rtc_warnings
)

# libpq (for PostgreSQL::PostgreSQL) may be needed explicitly on some platforms.
if(TARGET PostgreSQL::PostgreSQL)
    target_link_libraries(rtc_core PUBLIC PostgreSQL::PostgreSQL)
endif()

# ---------------------------------------------------------------------------
# realtime_chat_server — the deployable executable.
# ---------------------------------------------------------------------------
add_executable(realtime_chat_server main.cpp)
target_link_libraries(realtime_chat_server PRIVATE rtc_core rtc_warnings)
set_target_properties(realtime_chat_server PROPERTIES OUTPUT_NAME "realtime-chat")
