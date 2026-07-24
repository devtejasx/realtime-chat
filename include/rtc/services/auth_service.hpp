#pragma once

#include "rtc/dto/auth_dto.hpp"
#include "rtc/security/token_service.hpp"
#include "rtc/services/user_service.hpp"

namespace rtc::services {

// Orchestrates the authentication use-cases (register, login) by composing the
// user domain service with token issuance. Controllers call into this service
// with raw DTOs; validation, hashing, persistence and token minting are all
// coordinated here, keeping the HTTP layer thin.
class AuthService {
public:
    AuthService(UserService& user_service, const security::ITokenService& token_service) noexcept
        : user_service_(user_service), token_service_(token_service) {}

    // Validates + normalizes the request, creates the user, and issues tokens.
    [[nodiscard]] dto::AuthResponse register_user(dto::RegisterRequest request);

    // Validates + normalizes the request, verifies credentials, and issues tokens.
    [[nodiscard]] dto::AuthResponse login(dto::LoginRequest request);

private:
    [[nodiscard]] dto::AuthResponse build_response(const models::User& user) const;

    UserService& user_service_;
    const security::ITokenService& token_service_;
};

}  // namespace rtc::services
