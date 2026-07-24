#pragma once

#include <memory>
#include <string>

#include "rtc/config/config.hpp"
#include "rtc/controllers/auth_controller.hpp"
#include "rtc/controllers/health_controller.hpp"
#include "rtc/controllers/user_controller.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/repositories/user_repository.hpp"
#include "rtc/security/password_hasher.hpp"
#include "rtc/security/token_service.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/user_service.hpp"

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

    // Requests a graceful shutdown of the running server. Safe to call from a
    // signal handler context.
    void stop();

    [[nodiscard]] const config::Config& config() const noexcept { return config_; }

private:
    void run_migrations();
    void wire_object_graph();
    void register_routes();

    config::Config config_;
    std::string migrations_dir_;

    std::unique_ptr<database::ConnectionPool> pool_;
    std::unique_ptr<security::IPasswordHasher> password_hasher_;
    std::unique_ptr<security::ITokenService> token_service_;
    std::unique_ptr<repositories::IUserRepository> user_repository_;
    std::unique_ptr<services::UserService> user_service_;
    std::unique_ptr<services::AuthService> auth_service_;
    std::unique_ptr<middlewares::AuthMiddleware> auth_guard_;
    std::unique_ptr<controllers::HealthController> health_controller_;
    std::unique_ptr<controllers::AuthController> auth_controller_;
    std::unique_ptr<controllers::UserController> user_controller_;
    std::unique_ptr<http::App> app_;
    bool bootstrapped_ = false;
};

}  // namespace rtc
