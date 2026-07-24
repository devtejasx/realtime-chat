#include "rtc/config/config.hpp"

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "support/env_guard.hpp"

namespace {

using rtc::config::Config;
using rtc::errors::ConfigException;
using rtc::testing::EnvGuard;

TEST(ConfigTest, AppliesDevelopmentDefaultsWhenUnset) {
    EnvGuard p1("CHAT_PORT");
    EnvGuard p2("DB_HOST");
    EnvGuard p3("DB_PORT");
    EnvGuard p4("JWT_SECRET");
    EnvGuard p5("APP_ENV");
    EnvGuard p6("LOG_LEVEL");

    const Config config = Config::load_from_env();

    EXPECT_EQ(config.chat_port, 8080);
    EXPECT_EQ(config.db_host, "localhost");
    EXPECT_EQ(config.db_port, 5432);
    EXPECT_EQ(config.app_env, "development");
    EXPECT_FALSE(config.is_production());
}

TEST(ConfigTest, OverridesFromEnvironment) {
    EnvGuard port("CHAT_PORT", "9090");
    EnvGuard host("DB_HOST", "db.internal");
    EnvGuard name("DB_NAME", "chat_prod");
    EnvGuard level("LOG_LEVEL", "debug");

    const Config config = Config::load_from_env();

    EXPECT_EQ(config.chat_port, 9090);
    EXPECT_EQ(config.db_host, "db.internal");
    EXPECT_EQ(config.db_name, "chat_prod");
    EXPECT_EQ(config.log_level, "debug");
}

TEST(ConfigTest, RejectsNonNumericPort) {
    EnvGuard port("CHAT_PORT", "not-a-number");
    EXPECT_THROW(Config::load_from_env(), ConfigException);
}

TEST(ConfigTest, RejectsOutOfRangePort) {
    EnvGuard port("CHAT_PORT", "70000");
    EXPECT_THROW(Config::load_from_env(), ConfigException);
}

TEST(ConfigTest, RejectsDefaultSecretInProduction) {
    EnvGuard env("APP_ENV", "production");
    EnvGuard secret("JWT_SECRET", "dev-insecure-secret-change-me-in-production");
    EXPECT_THROW(Config::load_from_env(), ConfigException);
}

TEST(ConfigTest, RejectsShortSecretInProduction) {
    EnvGuard env("APP_ENV", "production");
    EnvGuard secret("JWT_SECRET", "tooshort");
    EXPECT_THROW(Config::load_from_env(), ConfigException);
}

TEST(ConfigTest, AcceptsStrongSecretInProduction) {
    EnvGuard env("APP_ENV", "production");
    EnvGuard secret("JWT_SECRET", "a-sufficiently-long-production-secret-value-01");
    const Config config = Config::load_from_env();
    EXPECT_TRUE(config.is_production());
}

TEST(ConfigTest, RedactedConnectionStringHidesPassword) {
    EnvGuard pw("DB_PASSWORD", "s3cr3t");
    const Config config = Config::load_from_env();
    const std::string redacted = config.database_connection_string_redacted();
    EXPECT_EQ(redacted.find("s3cr3t"), std::string::npos);
    EXPECT_NE(redacted.find("password=***"), std::string::npos);
}

}  // namespace
