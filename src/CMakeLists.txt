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
    errors/error_mapper.cpp
    utils/time.cpp
    security/bcrypt_password_hasher.cpp
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
    media/mime.cpp
    storage/local_file_storage.cpp
    realtime/session_manager.cpp
    realtime/room_manager.cpp
    realtime/connection_manager.cpp
    realtime/event_dispatcher.cpp
    realtime/heartbeat_monitor.cpp
    middlewares/auth_middleware.cpp
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
        bcrypt
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
