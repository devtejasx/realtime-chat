#include "rtc/application.hpp"

#include <filesystem>
#include <utility>

#include "rtc/database/migration_runner.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/repositories/pg_user_repository.hpp"
#include "rtc/security/bcrypt_password_hasher.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/utils/env.hpp"

namespace rtc {

Application::Application(config::Config config)
    : config_(std::move(config)),
      migrations_dir_(utils::get_env_or("MIGRATIONS_DIR", "migrations")) {}

Application::~Application() { stop(); }

void Application::bootstrap() {
    logging::init(config_.log_level);
    RTC_LOG_INFO("Starting realtime-chat {} in '{}' environment", RTC_VERSION, config_.app_env);
    RTC_LOG_INFO("Database target: {}", config_.database_connection_string_redacted());

    pool_ = std::make_unique<database::ConnectionPool>(config_.database_connection_string(),
                                                       config_.db_pool_size);
    run_migrations();
    wire_object_graph();
    register_routes();
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
    // Concrete implementations are chosen here and exposed to the rest of the
    // system only through their interfaces.
    password_hasher_ = std::make_unique<security::BcryptPasswordHasher>();

    token_service_ = std::make_unique<security::JwtTokenService>(security::JwtTokenService::Options{
        .secret = config_.jwt_secret,
        .issuer = config_.jwt_issuer,
        .access_ttl_seconds = config_.jwt_access_ttl_seconds,
        .refresh_ttl_seconds = config_.jwt_refresh_ttl_seconds,
    });

    user_repository_ = std::make_unique<repositories::PgUserRepository>(*pool_);
    user_service_ = std::make_unique<services::UserService>(*user_repository_, *password_hasher_);
    auth_service_ = std::make_unique<services::AuthService>(*user_service_, *token_service_);
    auth_guard_ = std::make_unique<middlewares::AuthMiddleware>(*token_service_);

    health_controller_ = std::make_unique<controllers::HealthController>(config_);
    auth_controller_ = std::make_unique<controllers::AuthController>(*auth_service_,
                                                                     *user_service_, *auth_guard_);

    app_ = std::make_unique<http::App>();
}

void Application::register_routes() {
    health_controller_->register_routes(*app_);
    auth_controller_->register_routes(*app_);
}

int Application::run() {
    if (!bootstrapped_) {
        bootstrap();
    }
    RTC_LOG_INFO("HTTP server listening on port {}", config_.chat_port);
    app_->loglevel(crow::LogLevel::Warning);
    app_->port(config_.chat_port).multithreaded().run();
    RTC_LOG_INFO("HTTP server stopped");
    logging::shutdown();
    return 0;
}

void Application::stop() {
    if (app_) {
        RTC_LOG_INFO("Graceful shutdown requested");
        app_->stop();
    }
}

}  // namespace rtc
