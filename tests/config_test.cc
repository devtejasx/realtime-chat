// tests/config_test.cc
// -----------------------------------------------------------------------------
// Unit tests for chat::Config.
//
// Config is the first thing that runs in production; if it mis-parses, the
// server boots wrong or not at all, so it earns thorough tests. Because
// FromEnvironment() accepts an injected EnvReader, these tests never touch
// the real process environment — no setenv() races, fully deterministic,
// safe to run in parallel.
// -----------------------------------------------------------------------------

#include "src/config/config.h"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace chat {
namespace {

// Builds an EnvReader backed by a plain map — our fake environment.
EnvReader FakeEnv(std::map<std::string, std::string> values) {
  return [values = std::move(values)](
             const std::string& name) -> std::optional<std::string> {
    const auto it = values.find(name);
    if (it == values.end()) return std::nullopt;
    return it->second;
  };
}

// Why: a developer must be able to run the server with zero variables set.
// This pins down every default so changing one is a conscious, reviewed act.
TEST(ConfigTest, UsesDefaultsWhenEnvironmentIsEmpty) {
  const Config config = Config::FromEnvironment(FakeEnv({}));

  EXPECT_EQ(config.environment, "development");
  EXPECT_EQ(config.host, "0.0.0.0");
  EXPECT_EQ(config.port, 8080);
  EXPECT_EQ(config.log_level, "info");
  EXPECT_GE(config.worker_threads, 1u);  // Exact count is machine-dependent.
}

// Why: proves each variable is actually read and mapped to the right field —
// catches copy-paste bugs like reading CHAT_PORT into worker_threads.
TEST(ConfigTest, ReadsEveryValueFromTheEnvironment) {
  const Config config = Config::FromEnvironment(FakeEnv({
      {"CHAT_ENVIRONMENT", "production"},
      {"CHAT_HOST", "127.0.0.1"},
      {"CHAT_PORT", "9000"},
      {"CHAT_WORKER_THREADS", "8"},
      {"CHAT_LOG_LEVEL", "warning"},
  }));

  EXPECT_EQ(config.environment, "production");
  EXPECT_EQ(config.host, "127.0.0.1");
  EXPECT_EQ(config.port, 9000);
  EXPECT_EQ(config.worker_threads, 8u);
  EXPECT_EQ(config.log_level, "warning");
}

// Why: "8080abc" or "eighty" must be a startup error, not silently 8080 or 0.
TEST(ConfigTest, RejectsNonNumericPort) {
  EXPECT_THROW(Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "eighty"}})),
               ConfigError);
  EXPECT_THROW(Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "8080abc"}})),
               ConfigError);
}

// Why: 70000 fits in the uint32 we parse into but not in a TCP port. Without
// the range check it would wrap to 4464 inside uint16_t — a server quietly
// listening on the wrong port is a miserable bug to chase.
TEST(ConfigTest, RejectsPortOutOfRange) {
  EXPECT_THROW(Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "0"}})),
               ConfigError);
  EXPECT_THROW(Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "70000"}})),
               ConfigError);
  EXPECT_THROW(Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "-1"}})),
               ConfigError);
}

// Why: zero worker threads would mean a server that accepts no connections.
TEST(ConfigTest, RejectsZeroOrInvalidWorkerThreads) {
  EXPECT_THROW(
      Config::FromEnvironment(FakeEnv({{"CHAT_WORKER_THREADS", "0"}})),
      ConfigError);
  EXPECT_THROW(
      Config::FromEnvironment(FakeEnv({{"CHAT_WORKER_THREADS", "many"}})),
      ConfigError);
}

// Why: a typo like CHAT_LOG_LEVEL=verbose should be caught at boot, not
// silently fall back to some default while the operator believes otherwise.
TEST(ConfigTest, RejectsUnknownLogLevel) {
  EXPECT_THROW(
      Config::FromEnvironment(FakeEnv({{"CHAT_LOG_LEVEL", "verbose"}})),
      ConfigError);
}

// Why: later milestones branch on environment (error detail, CORS, ...).
// An unrecognised stage name must never masquerade as development.
TEST(ConfigTest, RejectsUnknownEnvironmentName) {
  EXPECT_THROW(
      Config::FromEnvironment(FakeEnv({{"CHAT_ENVIRONMENT", "staging"}})),
      ConfigError);
}

// Why: the error message is an operator-facing interface — it must name the
// variable and the received value so 3 a.m. debugging is fast.
TEST(ConfigTest, ErrorMessageNamesVariableAndValue) {
  try {
    Config::FromEnvironment(FakeEnv({{"CHAT_PORT", "banana"}}));
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("CHAT_PORT"), std::string::npos) << message;
    EXPECT_NE(message.find("banana"), std::string::npos) << message;
  }
}

}  // namespace
}  // namespace chat
