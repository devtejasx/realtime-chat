// src/application.h
// -----------------------------------------------------------------------------
// Application — owns the Crow app object and the server lifecycle.
//
// Why a class instead of wiring routes in main()?
//   * No globals: the Crow app is a member, created and destroyed with the
//     Application object (RAII), never a file-scope singleton.
//   * Composition root: as milestones add controllers, services and
//     repositories, this is the one place where the object graph is
//     assembled and injected. main() stays a five-line shell.
//   * Testability: tests can construct an Application with any Config
//     without touching the real environment or starting a listener.
// -----------------------------------------------------------------------------

#ifndef REALTIME_CHAT_SRC_APPLICATION_H_
#define REALTIME_CHAT_SRC_APPLICATION_H_

#include "crow.h"
#include "src/config/config.h"

namespace chat {

class Application {
 public:
  // Takes the validated config by value and stores it for the lifetime of
  // the server. Routes are registered here, so the object is fully wired
  // the moment the constructor returns.
  explicit Application(Config config);

  // The app owns a live socket and thread pool once running — copying or
  // moving it has no sensible meaning, so both are forbidden.
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Binds to config.host:config.port and serves requests. Blocks until
  // Stop() is called or the process receives SIGINT/SIGTERM (Crow installs
  // its own handler).
  void Run();

  // Asks the event loop to shut down. Safe to call from another thread;
  // used by integration tests to tear the server down cleanly.
  void Stop();

 private:
  // Declares every HTTP route on app_. Called once from the constructor.
  void RegisterRoutes();

  const Config config_;
  crow::SimpleApp app_;
};

}  // namespace chat

#endif  // REALTIME_CHAT_SRC_APPLICATION_H_
