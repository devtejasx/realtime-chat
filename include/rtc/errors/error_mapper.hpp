#pragma once

#include <exception>

#include <crow/http_response.h>

#include "rtc/errors/exceptions.hpp"

namespace rtc::errors {

// Maps thrown exceptions to fully-formed Crow HTTP responses carrying the
// canonical JSON error envelope. This is the single choke point where domain
// errors become HTTP: controllers translate their try/catch here, and the
// global handler uses it as a last line of defence.
//
// AppException instances map to their declared status/code. Any other
// std::exception is treated as an unexpected internal error (500) and its
// message is deliberately NOT leaked to the client.
class ErrorMapper {
public:
    // Builds a response for a known application exception.
    [[nodiscard]] static crow::response to_response(const AppException& ex);

    // Builds a 500 response for an unexpected standard exception. The detailed
    // message is expected to be logged by the caller, not returned to clients.
    [[nodiscard]] static crow::response to_response(const std::exception& ex);

    // Builds a generic 500 response when no exception object is available.
    [[nodiscard]] static crow::response internal_error_response();
};

}  // namespace rtc::errors
