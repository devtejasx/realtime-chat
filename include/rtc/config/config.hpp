#pragma once

#include <cstdint>
#include <string>

namespace rtc::config {

// Strongly-typed, immutable application configuration.
//
// A Config is produced once at startup by Config::load_from_env() and then
// injected (by const reference) into the components that need it. All fields
// have development defaults so the server runs out of the box; production
// deployments override them via environment variables.
//
// Invariants are enforced at construction time by validate(): an invalid
// configuration throws rtc::errors::ConfigException and aborts startup, which
// is the correct fail-fast behaviour for a backend service.
class Config {
public:
    // HTTP server
    std::uint16_t chat_port = 8080;

    // PostgreSQL
    std::string db_host = "localhost";
    std::uint16_t db_port = 5432;
    std::string db_name = "realtime_chat";
    std::string db_user = "chat";
    std::string db_password = "chat_password";
    std::uint32_t db_pool_size = 8;

    // JWT
    std::string jwt_secret = "dev-insecure-secret-change-me-in-production";
    std::int64_t jwt_access_ttl_seconds = 900;            // 15 minutes
    std::int64_t jwt_refresh_ttl_seconds = 1'209'600;     // 14 days
    std::string jwt_issuer = "realtime-chat";

    // WebSocket heartbeat (Phase 2). Interval between server pings and the
    // idle timeout after which a silent connection is closed.
    std::int64_t ws_heartbeat_interval_seconds = 30;
    std::int64_t ws_heartbeat_timeout_seconds = 90;

    // Observability / environment
    std::string log_level = "info";
    std::string app_env = "development";

    // Builds Config from the process environment, applying defaults for any
    // unset variable, then validates. Throws ConfigException on invalid input.
    [[nodiscard]] static Config load_from_env();

    // Returns a libpqxx-compatible connection string. The password is included
    // because libpqxx requires it here; never log the result of this method.
    [[nodiscard]] std::string database_connection_string() const;

    // A redacted connection string safe for logging (password masked).
    [[nodiscard]] std::string database_connection_string_redacted() const;

    [[nodiscard]] bool is_production() const noexcept { return app_env == "production"; }

    // Validates cross-field invariants; throws ConfigException on failure.
    void validate() const;
};

}  // namespace rtc::config
