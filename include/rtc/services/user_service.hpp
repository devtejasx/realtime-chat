#pragma once

#include <cstdint>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/profile_dto.hpp"
#include "rtc/models/user.hpp"
#include "rtc/repositories/user_repository.hpp"
#include "rtc/security/password_hasher.hpp"

namespace rtc::services {

// Domain logic for users: registration and credential verification.
//
// Business rules live here — hashing on the way in, constant-time credential
// checking on the way out — while persistence stays in the repository and
// transport stays in the controller. Dependencies are injected as interfaces
// so the service is unit-testable with fakes.
class UserService {
public:
    UserService(repositories::IUserRepository& repository,
                const security::IPasswordHasher& hasher) noexcept
        : repository_(repository), hasher_(hasher) {}

    // Creates a user from a *validated, normalized* register request. Hashes the
    // password before persistence. Throws ConflictException when the username or
    // email is already taken.
    [[nodiscard]] models::User register_user(const dto::RegisterRequest& request);

    // Verifies credentials from a *validated, normalized* login request and
    // returns the authenticated user. Throws AuthenticationException on any
    // mismatch, without revealing whether the account exists.
    [[nodiscard]] models::User authenticate(const dto::LoginRequest& request);

    // Fetches a user by id, throwing NotFoundException when absent.
    [[nodiscard]] models::User get_by_id(std::int64_t id);

    // Validates + normalizes the request and applies a partial profile update
    // for the given user, returning the refreshed user. Throws
    // ValidationException on invalid input and NotFoundException if the user is
    // gone. This is the single entry point both REST and future callers use.
    [[nodiscard]] models::User update_profile(std::int64_t id,
                                              dto::UpdateProfileRequest request);

private:
    repositories::IUserRepository& repository_;
    const security::IPasswordHasher& hasher_;
};

}  // namespace rtc::services
