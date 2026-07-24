#include "rtc/services/auth_service.hpp"

#include <utility>

#include "rtc/dto/user_dto.hpp"
#include "rtc/validation/validators.hpp"

namespace rtc::services {

dto::AuthResponse AuthService::register_user(dto::RegisterRequest request) {
    validation::validate_and_normalize(request);
    const models::User user = user_service_.register_user(request);
    return build_response(user);
}

dto::AuthResponse AuthService::login(dto::LoginRequest request) {
    validation::validate_and_normalize(request);
    const models::User user = user_service_.authenticate(request);
    return build_response(user);
}

dto::AuthResponse AuthService::build_response(const models::User& user) const {
    dto::AuthResponse response;
    response.user = dto::UserResponse::from(user);
    response.tokens = token_service_.issue_pair(user.id, user.username);
    return response;
}

}  // namespace rtc::services
