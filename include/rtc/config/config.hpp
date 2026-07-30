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

    // --- Phase 3: scalability & production ---

    // Redis (distributed cache / sessions / presence). When disabled, or when
    // the binary is built without Redis support, the in-memory store is used.
    bool redis_enabled = false;
    std::string redis_url = "tcp://127.0.0.1:6379";

    // Cache TTLs (seconds).
    std::int64_t cache_user_ttl_seconds = 300;
    std::int64_t cache_conversation_ttl_seconds = 60;

    // File uploads / attachments.
    std::string upload_dir = "uploads";
    std::string upload_public_base_url = "/api/attachments";
    std::int64_t max_upload_bytes = 25 * 1024 * 1024;   // 25 MiB
    std::int64_t max_request_bytes = 1 * 1024 * 1024;   // 1 MiB (JSON bodies)

    // Rate limiting (fixed window). Zero disables a given limit.
    bool rate_limit_enabled = true;
    std::int64_t rate_limit_window_seconds = 60;
    std::int64_t rate_limit_login_max = 10;
    std::int64_t rate_limit_register_max = 5;
    std::int64_t rate_limit_message_max = 120;
    std::int64_t rate_limit_upload_max = 30;
    std::int64_t rate_limit_default_max = 300;

    // Security. Comma-separated allowed CORS origins ("*" allows any).
    std::string cors_allowed_origins = "*";

    // Background workers.
    std::int64_t worker_threads = 2;
    // Interval for periodic maintenance jobs (cache purge, session cleanup).
    std::int64_t maintenance_interval_seconds = 60;

    // --- Phase 5: enterprise platform ---

    // Distributed tracing (OpenTelemetry-compatible). Disabled by default: it is
    // an opt-in operational capability, and a tracer pointed at a collector that
    // is not there should never be the default experience.
    bool tracing_enabled = false;
    // "logging" (structured log records), "otlp" (OTLP/HTTP JSON — Jaeger, Tempo,
    // OpenTelemetry Collector) or "zipkin" (Zipkin v2 JSON).
    std::string tracing_exporter = "logging";
    std::string tracing_endpoint = "http://127.0.0.1:4318/v1/traces";
    // Head-based sampling probability for root spans, in [0, 1]. 5% keeps trace
    // volume affordable while still surfacing latency outliers; raise it while
    // investigating, and set it to 1.0 in staging.
    double tracing_sample_ratio = 0.05;

    // Cross-instance WebSocket fan-out via Redis Pub/Sub. Required for more than
    // one replica; pointless (pure overhead) with exactly one. Defaults to
    // following redis_enabled, since Redis is its prerequisite.
    bool cluster_enabled = false;

    // How long an authorisation decision (role, suspension) may be cached. This
    // is the worst-case delay before a demotion or ban takes effect, so keep it
    // short — see services::AuthorizationService.
    std::int64_t authz_cache_ttl_seconds = 30;

    // Observability / environment
    std::string log_level = "info";
    // Log output format: "text" (human-readable) or "json" (structured, for log
    // aggregation in production).
    std::string log_format = "text";
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
