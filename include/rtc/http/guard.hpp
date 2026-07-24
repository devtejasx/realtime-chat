#pragma once

#include <exception>
#include <utility>

#include <crow/http_response.h>

#include "rtc/errors/error_mapper.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"

namespace rtc::http {

// Wraps a handler body so every controller shares one exception-to-HTTP
// translation policy: application exceptions become their declared status
// (logged at warn), and unexpected exceptions become a masked 500 (logged at
// error with full detail). Handlers therefore contain only happy-path logic.
template <typename Fn>
[[nodiscard]] crow::response run_guarded(Fn&& fn) {
    try {
        return std::forward<Fn>(fn)();
    } catch (const errors::AppException& ex) {
        RTC_LOG_WARN("Request failed [{}]: {}{}", ex.code(), ex.message(),
                     ex.has_details() ? " (" + ex.details() + ")" : "");
        return errors::ErrorMapper::to_response(ex);
    } catch (const std::exception& ex) {
        RTC_LOG_ERROR("Unhandled exception in handler: {}", ex.what());
        return errors::ErrorMapper::to_response(ex);
    }
}

}  // namespace rtc::http
