// src/application.cc
// -----------------------------------------------------------------------------
// Application implementation: route registration and server lifecycle.
// -----------------------------------------------------------------------------

#include "src/application.h"

#include <string>
#include <utility>

namespace chat {
namespace {

// Maps our validated config string onto Crow's enum. Config guarantees the
// string is one of the allowed values, so the fallback branch is defensive
// only — it can't be reached through FromEnvironment().
crow::LogLevel ToCrowLogLevel(const std::string& level) {
  if (level == "debug") return crow::LogLevel::Debug;
  if (level == "warning") return crow::LogLevel::Warning;
  if (level == "error") return crow::LogLevel::Error;
  if (level == "critical") return crow::LogLevel::Critical;
  return crow::LogLevel::Info;
}

}  // namespace

Application::Application(Config config) : config_(std::move(config)) {
  app_.loglevel(ToCrowLogLevel(config_.log_level));
  RegisterRoutes();
}

void Application::RegisterRoutes() {
  // GET /health — liveness probe.
  //
  // Every service exposes one of these: load balancers, container
  // orchestrators and uptime monitors poll it to decide whether the process
  // is alive. It must be unauthenticated, cheap, and dependency-free.
  // (A separate /ready endpoint that checks Postgres/Redis connectivity
  // comes with Milestone 2 — liveness and readiness are different questions.)
  CROW_ROUTE(app_, "/health")([this] {
    crow::json::wvalue body;
    body["status"] = "ok";
    body["service"] = "realtime-chat";
    body["version"] = CHAT_VERSION;
    body["environment"] = config_.environment;
    return body;
  });
}

void Application::Run() {
  CROW_LOG_INFO << "realtime-chat " << CHAT_VERSION << " listening on "
                << config_.host << ":" << config_.port << " ("
                << config_.worker_threads << " worker threads, "
                << config_.environment << ")";
  app_.bindaddr(config_.host)
      .port(config_.port)
      .concurrency(config_.worker_threads)
      .run();
}

void Application::Stop() { app_.stop(); }

}  // namespace chat
