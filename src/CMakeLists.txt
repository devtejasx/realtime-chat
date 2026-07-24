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
    dto/auth_dto.cpp
    validation/validators.cpp
    database/connection_pool.cpp
    database/migration_runner.cpp
    repositories/pg_user_repository.cpp
    services/user_service.cpp
    services/auth_service.cpp
    middlewares/auth_middleware.cpp
    controllers/health_controller.cpp
    controllers/auth_controller.cpp
    application.cpp
)

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
