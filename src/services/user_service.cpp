#include "rtc/services/user_service.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/validation/validators.hpp"

namespace rtc::services {
namespace {

// A precomputed bcrypt hash of a random string. When a login references a
// non-existent account we still run a verification against this dummy hash so
// the response time does not reveal whether the account exists (mitigating
// user-enumeration via timing).
constexpr const char* kDummyHash =
    "$2b$12$C6UzMDM.H6dfI/f/IKcEeO3f0i1E1r5aI3l0m9n8o7p6q5r4s3t2u";

}  // namespace

models::User UserService::register_user(const dto::RegisterRequest& request) {
    repositories::NewUser input;
    input.username = request.username;
    input.email = request.email;
    input.password_hash = hasher_.hash(request.password);
    // Uniqueness is enforced atomically by the repository (unique indexes),
    // which surfaces a ConflictException — no check-then-act race here.
    return repository_.create(input);
}

models::User UserService::authenticate(const dto::LoginRequest& request) {
    const auto user = repository_.find_by_identifier(request.identifier);
    if (!user) {
        // Equalise timing with the success path before failing.
        (void) hasher_.verify(request.password, kDummyHash);
        throw rtc::errors::AuthenticationException("Invalid credentials");
    }
    if (!hasher_.verify(request.password, user->password_hash)) {
        throw rtc::errors::AuthenticationException("Invalid credentials");
    }
    return *user;
}

models::User UserService::get_by_id(std::int64_t id) {
    const auto user = repository_.find_by_id(id);
    if (!user) {
        throw rtc::errors::NotFoundException("User not found",
                                             "user_id=" + std::to_string(id));
    }
    return *user;
}

models::User UserService::update_profile(std::int64_t id, dto::UpdateProfileRequest request) {
    validation::validate_and_normalize(request);

    repositories::ProfileUpdate update;
    update.display_name_set = request.display_name_set;
    update.display_name = request.display_name;
    update.bio_set = request.bio_set;
    update.bio = request.bio;
    update.avatar_url_set = request.avatar_url_set;
    update.avatar_url = request.avatar_url;

    return repository_.update_profile(id, update);
}

}  // namespace rtc::services
