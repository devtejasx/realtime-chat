#pragma once

#include <crow/app.h>

#include "rtc/middlewares/logging_middleware.hpp"

namespace rtc::http {

// The concrete Crow application type for this service, with its global
// middleware stack baked in. Controllers accept `App&` and register routes on
// it. Centralising the alias means the middleware list is defined in exactly
// one place.
using App = crow::App<middlewares::LoggingMiddleware>;

}  // namespace rtc::http
