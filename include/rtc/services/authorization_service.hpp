#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "rtc/cache/cache_store.hpp"
#include "rtc/repositories/user_admin_repository.hpp"
#include "rtc/security/role.hpp"

namespace rtc::services {

// Answers "may this user do this?" and "is this account suspended?".
//
// Why the role is resolved from the database rather than carried in the JWT
// -----------------------------------------------------------------------
// Putting the role in the access token would make authorisation free, and it is
// what most tutorials do. It is also wrong for anything that must be revocable:
// a token is a bearer credential valid until it expires, so a demoted moderator
// or a banned user would keep their old rights for the remainder of the access
// TTL (15 minutes by default). "Banned" that takes effect in a quarter of an
// hour is not banned.
//
// So the store is authoritative and every authorisation decision reads it —
// behind a short-TTL cache so the cost is one cache hit, not one query, per
// request. Mutations call invalidate() to make a change effective immediately,
// and the TTL is the worst-case staleness if an invalidation is ever missed
// (for instance when another instance performs the change and the cache backend
// is process-local rather than Redis).
//
// Thread-safe: the cache store and the repository are both thread-safe, and this
// class holds only immutable configuration plus atomic counters.
class AuthorizationService {
public:
    struct Options {
        // Worst-case staleness of a role/ban decision. Deliberately short.
        std::chrono::seconds cache_ttl{30};
    };

    // Options is passed explicitly (matching JwtTokenService and
    // AttachmentService). A defaulted `Options = {}` argument is not an option
    // here: a nested type's default member initialisers are not usable inside the
    // enclosing class definition, so the default argument would not compile.
    AuthorizationService(repositories::IUserAdminRepository& users, cache::ICacheStore& cache,
                         Options options) noexcept
        : users_(users), cache_(cache), options_(options) {}

    AuthorizationService(const AuthorizationService&) = delete;
    AuthorizationService& operator=(const AuthorizationService&) = delete;

    // The user's role. Falls back to kUser when the account cannot be resolved,
    // so an unexpected lookup miss can never grant elevated rights.
    [[nodiscard]] security::Role role_of(std::int64_t user_id);

    [[nodiscard]] bool has_permission(std::int64_t user_id, security::Permission permission);

    // Throws AuthorizationException (403) unless the user holds `permission`.
    // The guard controllers call; it names the missing permission in `details`
    // so a client can report something actionable.
    void require_permission(std::int64_t user_id, security::Permission permission);

    [[nodiscard]] bool is_banned(std::int64_t user_id);

    // Throws AuthenticationException (401) when the account is suspended.
    // Suspension is an authentication-level fact — the credential is no longer
    // valid at all — rather than a per-action authorisation failure.
    void require_active(std::int64_t user_id);

    // Drops cached role/ban state for a user. Call after any mutation.
    void invalidate(std::int64_t user_id);

    // Cache effectiveness, surfaced on /metrics.
    [[nodiscard]] std::uint64_t cache_hits() const noexcept { return hits_.load(); }
    [[nodiscard]] std::uint64_t cache_misses() const noexcept { return misses_.load(); }

private:
    [[nodiscard]] static std::string role_key(std::int64_t user_id);
    [[nodiscard]] static std::string ban_key(std::int64_t user_id);

    repositories::IUserAdminRepository& users_;
    cache::ICacheStore& cache_;
    Options options_;
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> misses_{0};
};

}  // namespace rtc::services
