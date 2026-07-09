// src/config/config.cc
// -----------------------------------------------------------------------------
// Parsing and validation of environment-provided configuration.
//
// Philosophy: validate everything at startup. Every value that reaches the
// rest of the codebase through Config is already known-good, so downstream
// code never re-checks it. "Parse, don't validate."
// -----------------------------------------------------------------------------

#include "src/config/config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <thread>

namespace chat {
namespace {

// Reads from the real process environment. Defined here (not in the header)
// so the std::getenv dependency stays in exactly one translation unit.
std::optional<std::string> ReadProcessEnv(const std::string& name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe) — read-only use at startup.
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) return std::nullopt;
  return std::string(value);
}

// Parses `text` as an integral T using std::from_chars — unlike std::stoi
// this never throws surprise exceptions, accepts no leading whitespace, and
// detects trailing garbage ("8080abc" is an error, not 8080).
template <typename T>
std::optional<T> ParseNumber(const std::string& text) {
  T result{};
  const char* end = text.data() + text.size();
  auto [ptr, ec] = std::from_chars(text.data(), end, result);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return result;
}

// Helper for uniform error text: which variable, which value, what we expect.
[[noreturn]] void FailInvalid(const std::string& name, const std::string& value,
                              const std::string& expectation) {
  throw ConfigError("invalid value \"" + value + "\" for " + name + ": " +
                    expectation);
}

}  // namespace

unsigned int Config::DefaultWorkerThreads() {
  return std::max(1u, std::thread::hardware_concurrency());
}

Config Config::FromEnvironment() { return FromEnvironment(ReadProcessEnv); }

Config Config::FromEnvironment(const EnvReader& read_env) {
  Config config;  // Starts with the defaults declared in the header.

  if (const auto value = read_env("CHAT_ENVIRONMENT")) {
    static constexpr std::array kAllowed = {"development", "test",
                                            "production"};
    if (std::find(kAllowed.begin(), kAllowed.end(), *value) == kAllowed.end()) {
      FailInvalid("CHAT_ENVIRONMENT", *value,
                  "expected development, test or production");
    }
    config.environment = *value;
  }

  if (const auto value = read_env("CHAT_HOST")) {
    if (value->empty()) {
      FailInvalid("CHAT_HOST", *value, "expected a non-empty bind address");
    }
    config.host = *value;
  }

  if (const auto value = read_env("CHAT_PORT")) {
    // Parse into a wider type first so 70000 is caught as out-of-range
    // instead of silently wrapping around inside uint16_t.
    const auto port = ParseNumber<std::uint32_t>(*value);
    if (!port || *port == 0 || *port > 65535) {
      FailInvalid("CHAT_PORT", *value, "expected an integer in [1, 65535]");
    }
    config.port = static_cast<std::uint16_t>(*port);
  }

  if (const auto value = read_env("CHAT_WORKER_THREADS")) {
    const auto threads = ParseNumber<unsigned int>(*value);
    if (!threads || *threads == 0) {
      FailInvalid("CHAT_WORKER_THREADS", *value,
                  "expected a positive integer");
    }
    config.worker_threads = *threads;
  }

  if (const auto value = read_env("CHAT_LOG_LEVEL")) {
    static constexpr std::array kAllowed = {"debug", "info", "warning",
                                            "error", "critical"};
    if (std::find(kAllowed.begin(), kAllowed.end(), *value) == kAllowed.end()) {
      FailInvalid("CHAT_LOG_LEVEL", *value,
                  "expected debug, info, warning, error or critical");
    }
    config.log_level = *value;
  }

  return config;
}

}  // namespace chat
