#include "rtc/config/config.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "rtc/errors/exceptions.hpp"
#include "rtc/utils/env.hpp"

namespace rtc::config {
namespace {

using rtc::errors::ConfigException;

// Parses an unsigned integer environment variable, returning `fallback` when
// the variable is unset/empty. Throws ConfigException when a value is present
// but not a valid integer within [min, max].
template <typename T>
[[nodiscard]] T parse_uint_env(const char* name, T fallback, T min_value, T max_value) {
    const auto raw = rtc::utils::get_env(name);
    if (!raw || raw->empty()) {
        return fallback;
    }

    std::uint64_t parsed = 0;
    const char* begin = raw->data();
    const char* end = begin + raw->size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        throw ConfigException("Invalid integer for environment variable",
                              std::string(name) + "='" + *raw + "'");
    }
    if (parsed < static_cast<std::uint64_t>(min_value) ||
        parsed > static_cast<std::uint64_t>(max_value)) {
        throw ConfigException("Value out of range for environment variable",
                              std::string(name) + "='" + *raw + "'");
    }
    return static_cast<T>(parsed);
}

// Parses a signed 64-bit integer environment variable with the same semantics.
[[nodiscard]] std::int64_t parse_int64_env(const char* name, std::int64_t fallback,
                                           std::int64_t min_value) {
    const auto raw = rtc::utils::get_env(name);
    if (!raw || raw->empty()) {
        return fallback;
    }

    std::int64_t parsed = 0;
    const char* begin = raw->data();
    const char* end = begin + raw->size();
    const auto [ptr, ec] = std::from_chars(begin, end, parsed);
    if (ec != std::errc{} || ptr != end) {
        throw ConfigException("Invalid integer for environment variable",
                              std::string(name) + "='" + *raw + "'");
    }
    if (parsed < min_value) {
        throw ConfigException("Value out of range for environment variable",
                              std::string(name) + "='" + *raw + "'");
    }
    return parsed;
}

}  // namespace

Config Config::load_from_env() {
    Config cfg;

    cfg.chat_port = parse_uint_env<std::uint16_t>(
        "CHAT_PORT", cfg.chat_port, 1, std::numeric_limits<std::uint16_t>::max());

    cfg.db_host = rtc::utils::get_env_or("DB_HOST", cfg.db_host);
    cfg.db_port = parse_uint_env<std::uint16_t>(
        "DB_PORT", cfg.db_port, 1, std::numeric_limits<std::uint16_t>::max());
    cfg.db_name = rtc::utils::get_env_or("DB_NAME", cfg.db_name);
    cfg.db_user = rtc::utils::get_env_or("DB_USER", cfg.db_user);
    cfg.db_password = rtc::utils::get_env_or("DB_PASSWORD", cfg.db_password);
    cfg.db_pool_size = parse_uint_env<std::uint32_t>("DB_POOL_SIZE", cfg.db_pool_size, 1, 512);

    cfg.jwt_secret = rtc::utils::get_env_or("JWT_SECRET", cfg.jwt_secret);
    cfg.jwt_access_ttl_seconds =
        parse_int64_env("JWT_ACCESS_TTL_SECONDS", cfg.jwt_access_ttl_seconds, 1);
    cfg.jwt_refresh_ttl_seconds =
        parse_int64_env("JWT_REFRESH_TTL_SECONDS", cfg.jwt_refresh_ttl_seconds, 1);
    cfg.jwt_issuer = rtc::utils::get_env_or("JWT_ISSUER", cfg.jwt_issuer);

    cfg.log_level = rtc::utils::get_env_or("LOG_LEVEL", cfg.log_level);
    cfg.app_env = rtc::utils::get_env_or("APP_ENV", cfg.app_env);

    cfg.validate();
    return cfg;
}

void Config::validate() const {
    if (jwt_secret.empty()) {
        throw ConfigException("JWT_SECRET must not be empty");
    }
    // Refuse to boot in production with the shipped insecure default or a weak
    // secret; this is a common and dangerous misconfiguration.
    if (is_production()) {
        if (jwt_secret == "dev-insecure-secret-change-me-in-production") {
            throw ConfigException("JWT_SECRET must be overridden in production");
        }
        if (jwt_secret.size() < 32) {
            throw ConfigException("JWT_SECRET must be at least 32 bytes in production");
        }
    }
    if (jwt_refresh_ttl_seconds < jwt_access_ttl_seconds) {
        throw ConfigException("JWT refresh TTL must be >= access TTL");
    }
    if (db_name.empty() || db_user.empty() || db_host.empty()) {
        throw ConfigException("Database host, name and user must not be empty");
    }
}

std::string Config::database_connection_string() const {
    // Key/value connection string form understood by libpq / libpqxx.
    return "host=" + db_host + " port=" + std::to_string(db_port) + " dbname=" + db_name +
           " user=" + db_user + " password=" + db_password +
           " connect_timeout=5 application_name=realtime-chat";
}

std::string Config::database_connection_string_redacted() const {
    return "host=" + db_host + " port=" + std::to_string(db_port) + " dbname=" + db_name +
           " user=" + db_user + " password=*** application_name=realtime-chat";
}

}  // namespace rtc::config
