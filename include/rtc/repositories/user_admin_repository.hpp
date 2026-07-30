#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtc/dto/pagination.hpp"
#include "rtc/models/user.hpp"
#include "rtc/security/role.hpp"
#include "rtc/utils/time.hpp"

namespace rtc::repositories {

// A user as an administrator sees them: the public model plus the authorisation
// attributes added by migration 0011.
struct AdminUserRecord {
    models::User user;
    security::Role role = security::kDefaultRole;
    std::optional<utils::TimePoint> banned_at;
    std::optional<std::string> ban_reason;
    std::optional<std::int64_t> banned_by;

    [[nodiscard]] bool is_banned() const noexcept { return banned_at.has_value(); }
};

// Search/filter criteria for the admin user list. Fields are ANDed.
struct AdminUserFilter {
    // Case-insensitive substring across username, email and display name.
    std::optional<std::string> query;
    std::optional<security::Role> role;
    std::optional<bool> banned;
};

// Administrative persistence boundary for users.
//
// Deliberately *separate* from IUserRepository rather than an extension of it.
// Two reasons:
//
//   1. Backward compatibility — IUserRepository is implemented by fakes in the
//      existing test suite. Adding methods there would break every one of them.
//   2. Separation of concerns — the ordinary user repository serves the
//      self-service surface (register, profile, login lookup). Reading and
//      mutating another account's role or suspension state is a different
//      capability with a different authorisation story, and keeping it behind a
//      distinct interface means an ordinary service cannot reach it at all.
class IUserAdminRepository {
  public:
    virtual ~IUserAdminRepository() = default;

    // The authorisation hot path: resolve a user's role. Returns nullopt when the
    // user does not exist. An unparseable stored value fails closed to kUser.
    [[nodiscard]] virtual std::optional<security::Role> find_role(std::int64_t user_id) = 0;

    // Whether the account is currently suspended. nullopt when the user is gone.
    [[nodiscard]] virtual std::optional<bool> is_banned(std::int64_t user_id) = 0;

    [[nodiscard]] virtual std::optional<AdminUserRecord> find(std::int64_t user_id) = 0;

    [[nodiscard]] virtual std::vector<AdminUserRecord> list(const AdminUserFilter& filter,
                                                            const dto::Pagination& page) = 0;
    [[nodiscard]] virtual std::int64_t count(const AdminUserFilter& filter) = 0;

    // Assigns a role, returning the previous one so the caller can emit an
    // accurate audit event. Throws NotFoundException when the user is missing.
    [[nodiscard]] virtual security::Role set_role(std::int64_t user_id, security::Role role) = 0;

    // Suspends or reinstates an account. `reason` and `actor_id` are recorded
    // only when banning. Throws NotFoundException when the user is missing.
    virtual void set_banned(std::int64_t user_id,
                            bool banned,
                            std::optional<std::string> reason,
                            std::optional<std::int64_t> actor_id) = 0;

    // Role histogram for the admin dashboard.
    [[nodiscard]] virtual std::vector<std::pair<security::Role, std::int64_t>> counts_by_role() = 0;
};

}  // namespace rtc::repositories
