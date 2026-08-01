#include "rtc/services/authorization_service.hpp"

#include <string>

#include "rtc/errors/exceptions.hpp"
#include "rtc/tracing/scoped_span.hpp"

namespace rtc::services {
namespace {

constexpr const char* kRoleKeyPrefix = "authz:role:";
constexpr const char* kBanKeyPrefix = "authz:banned:";

}  // namespace

std::string AuthorizationService::role_key(std::int64_t user_id) {
    return kRoleKeyPrefix + std::to_string(user_id);
}

std::string AuthorizationService::ban_key(std::int64_t user_id) {
    return kBanKeyPrefix + std::to_string(user_id);
}

security::Role AuthorizationService::role_of(std::int64_t user_id) {
    const std::string key = role_key(user_id);
    if (const auto cached = cache_.get(key); cached.has_value()) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        // A cached value that no longer parses (rolling deploy renamed a role,
        // say) is treated as a miss rather than silently downgraded.
        if (const auto parsed = security::parse_role(*cached); parsed.has_value()) {
            return *parsed;
        }
    }

    misses_.fetch_add(1, std::memory_order_relaxed);
    auto scope = tracing::db_scope("authz.find_role");
    const auto role = users_.find_role(user_id).value_or(security::kDefaultRole);
    cache_.set(key, security::to_string(role), options_.cache_ttl);
    return role;
}

bool AuthorizationService::has_permission(std::int64_t user_id, security::Permission permission) {
    return security::has_permission(role_of(user_id), permission);
}

void AuthorizationService::require_permission(std::int64_t user_id,
                                              security::Permission permission) {
    const security::Role role = role_of(user_id);
    if (!security::has_permission(role, permission)) {
        throw errors::AuthorizationException(
            "Insufficient permissions",
            "required=" + std::string(security::to_string(permission)) +
                " role=" + std::string(security::to_string(role)));
    }
}

bool AuthorizationService::is_banned(std::int64_t user_id) {
    const std::string key = ban_key(user_id);
    if (const auto cached = cache_.get(key); cached.has_value()) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        return *cached == "1";
    }

    misses_.fetch_add(1, std::memory_order_relaxed);
    auto scope = tracing::db_scope("authz.is_banned");
    // A missing user is not "banned" — it is a stale token for a deleted
    // account. That distinction belongs to the caller, which will fail the
    // lookup on its own terms; here it is simply not a suspension.
    const bool banned = users_.is_banned(user_id).value_or(false);
    cache_.set(key, banned ? "1" : "0", options_.cache_ttl);
    return banned;
}

void AuthorizationService::require_active(std::int64_t user_id) {
    if (is_banned(user_id)) {
        throw errors::AuthenticationException("Account is suspended",
                                              "user_id=" + std::to_string(user_id));
    }
}

void AuthorizationService::invalidate_local(std::int64_t user_id) {
    cache_.del(role_key(user_id));
    cache_.del(ban_key(user_id));
}

void AuthorizationService::invalidate(std::int64_t user_id) {
    invalidate_local(user_id);
    // Announced after the local drop, so this instance is already consistent by
    // the time anyone else acts on the notice. The publisher is noexcept: a
    // cluster hop that fails must not turn a successful ban into a 500.
    invalidations_->publish_invalidation(
        cache::InvalidationEvent{.scope = std::string(cache::invalidation_scopes::kAuthorization),
                                 .key = std::to_string(user_id),
                                 .sub_key = {}});
}

}  // namespace rtc::services
