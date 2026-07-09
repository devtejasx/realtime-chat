// src/config/config.h
// -----------------------------------------------------------------------------
// Application configuration, loaded from the process environment.
//
// We follow the 12-factor-app rule: configuration lives in environment
// variables, never in source code. The same binary then runs unchanged on a
// laptop, in Docker Compose, and on EC2 — only the environment differs.
//
// Testability: FromEnvironment() takes an EnvReader function instead of
// calling std::getenv directly. Production passes the real environment;
// tests pass a lambda backed by a plain map. This is dependency injection in
// its simplest form — inject the *side effect*, keep the logic pure.
// -----------------------------------------------------------------------------

#ifndef REALTIME_CHAT_SRC_CONFIG_CONFIG_H_
#define REALTIME_CHAT_SRC_CONFIG_CONFIG_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace chat {

// Thrown when an environment variable holds a value we cannot use (bad port,
// unknown log level, ...). Startup code catches this at the top level and
// exits with a clear message — a server with broken config must fail fast,
// not limp along with guessed values.
class ConfigError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Looks up one variable by name; std::nullopt means "not set".
// Signature is the seam that lets tests fake the environment.
using EnvReader =
    std::function<std::optional<std::string>(const std::string& name)>;

// Immutable-after-construction bag of settings. Plain struct on purpose:
// there is no invariant beyond what FromEnvironment() validates, so getters
// and setters would be ceremony without benefit.
struct Config {
  // Deployment stage: "development", "test" or "production".
  // Later milestones use this to toggle behaviour (e.g. error verbosity).
  std::string environment = "development";

  // Interface to bind. 0.0.0.0 = all interfaces, which is what a
  // containerised server wants; a desktop-only run may prefer 127.0.0.1.
  std::string host = "0.0.0.0";

  // TCP port for the HTTP/WebSocket listener.
  std::uint16_t port = 8080;

  // Size of Crow's worker-thread pool. 0 is invalid; the default asks the
  // OS how many hardware threads exist.
  unsigned int worker_threads = DefaultWorkerThreads();

  // Minimum severity that gets logged: debug|info|warning|error|critical.
  std::string log_level = "info";

  // Builds a Config from the real process environment (std::getenv).
  static Config FromEnvironment();

  // Builds a Config from an injected environment. Every unset variable
  // keeps its default; every set variable is parsed and validated.
  // Throws ConfigError on any invalid value.
  static Config FromEnvironment(const EnvReader& read_env);

  // std::thread::hardware_concurrency(), clamped to at least 1 because the
  // standard allows it to return 0 when the count is unknown.
  static unsigned int DefaultWorkerThreads();
};

}  // namespace chat

#endif  // REALTIME_CHAT_SRC_CONFIG_CONFIG_H_
