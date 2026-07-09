// src/main.cc
// -----------------------------------------------------------------------------
// Process entry point.
//
// Deliberately thin: load config, build the Application, run it. All logic
// lives in classes that the test suite can link against; main() itself has
// nothing worth testing.
// -----------------------------------------------------------------------------

#include <cstdlib>
#include <exception>
#include <iostream>

#include "src/application.h"
#include "src/config/config.h"

int main() {
  try {
    // Fail fast: a misconfigured server should die at startup with a clear
    // message, not serve traffic with guessed values.
    chat::Application application(chat::Config::FromEnvironment());
    application.Run();  // Blocks until shutdown (SIGINT/SIGTERM or Stop()).
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    // Last-resort handler. Write to stderr so container runtimes and
    // systemd capture the reason for the non-zero exit.
    std::cerr << "fatal: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
