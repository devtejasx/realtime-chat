#include "rtc/errors/error_mapper.hpp"

#include <string>

#include "rtc/errors/error_response.hpp"

namespace rtc::errors {
namespace {

// Serialises a JSON error body into a Crow response with the given status and
// a JSON content type.
[[nodiscard]] crow::response build(int status, const nlohmann::json& body) {
    crow::response response(status, body.dump());
    response.set_header("Content-Type", "application/json");
    return response;
}

}  // namespace

crow::response ErrorMapper::to_response(const AppException& ex) {
    return build(ex.http_status(), make_error_body(ex));
}

crow::response ErrorMapper::to_response(const std::exception&) {
    // Never surface internal exception text to clients; log it upstream instead.
    return internal_error_response();
}

crow::response ErrorMapper::internal_error_response() {
    return build(500, make_error_body(code_for(ErrorType::kInternal),
                                      "An unexpected internal error occurred"));
}

}  // namespace rtc::errors
