#include <exception>

#include "rtc/application.hpp"
#include "rtc/config/config.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/utils/env.hpp"

// Process entry point.
//
// Loads configuration from the environment, constructs the application, and
// runs the HTTP server. Crow installs its own SIGINT/SIGTERM handlers to stop
// the server, after which run() returns and resources unwind via RAII. Any
// fatal startup error is logged and mapped to a non-zero exit code.
int main() {
    // Initialise logging early (before config parsing) so configuration errors
    // are visible. The level is refined once Config is loaded.
    rtc::logging::init(rtc::utils::get_env_or("LOG_LEVEL", "info"));

    try {
        auto config = rtc::config::Config::load_from_env();
        rtc::Application app(std::move(config));
        return app.run();
    } catch (const rtc::errors::ConfigException& ex) {
        RTC_LOG_CRITICAL("Configuration error: {}{}", ex.message(),
                         ex.has_details() ? " (" + ex.details() + ")" : "");
        return 78;  // EX_CONFIG
    } catch (const rtc::errors::AppException& ex) {
        RTC_LOG_CRITICAL("Fatal application error [{}]: {}", ex.code(), ex.message());
        return 1;
    } catch (const std::exception& ex) {
        RTC_LOG_CRITICAL("Fatal error: {}", ex.what());
        return 1;
    }
}
