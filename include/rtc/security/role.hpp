#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rtc::security {

// Role-based access control.
//
// Roles are a strict hierarchy — each tier holds every permission of the tier
// below plus its own — but the hierarchy is *derived from* the permission table
// rather than assumed by comparing roles. Comparing role ordinals ("is my role
// >= Moderator?") is the classic RBAC mistake: it couples every call site to the
// ordering, and adding a role in the middle silently re-authorises code paths.
// Call sites therefore ask about a Permission, never about a Role.
enum class Role : std::uint8_t {
    kUser = 0,
    kModerator = 1,
    kAdmin = 2,
    kSuperAdmin = 3,
};

inline constexpr std::size_t kRoleCount = 4;

// Default for every existing and newly-registered account.
inline constexpr Role kDefaultRole = Role::kUser;

[[nodiscard]] constexpr std::string_view to_string(Role role) noexcept {
    switch (role) {
        case Role::kUser:
            return "user";
        case Role::kModerator:
            return "moderator";
        case Role::kAdmin:
            return "admin";
        case Role::kSuperAdmin:
            return "super_admin";
    }
    return "user";
}

// Parses a stored/API role name. Returns nullopt for anything unrecognised —
// callers must decide explicitly, and the persistence layer deliberately fails
// closed to kUser rather than granting an unknown role elevated rights.
[[nodiscard]] constexpr std::optional<Role> parse_role(std::string_view name) noexcept {
    if (name == "user") return Role::kUser;
    if (name == "moderator") return Role::kModerator;
    if (name == "admin") return Role::kAdmin;
    if (name == "super_admin") return Role::kSuperAdmin;
    return std::nullopt;
}

// Discrete capabilities. Keep kCount last.
enum class Permission : std::size_t {
    // Messaging / moderation
    kDeleteAnyMessage,
    kEditAnyMessage,
    kViewAnyConversation,
    // Group administration
    kManageGroups,
    // User administration
    kBanUsers,
    kManageUsers,
    kManageRoles,
    // Sessions
    kViewSessions,
    kRevokeSessions,
    // Operations / observability
    kViewAuditLogs,
    kViewSystemMetrics,
    kManageFeatureFlags,
    kCount,
};

inline constexpr std::size_t kPermissionCount = static_cast<std::size_t>(Permission::kCount);

static_assert(kPermissionCount <= 32,
              "The permission bitmask is a std::uint32_t; widen it before adding a 33rd "
              "permission.");

[[nodiscard]] constexpr std::string_view to_string(Permission permission) noexcept {
    switch (permission) {
        case Permission::kDeleteAnyMessage:
            return "message.delete_any";
        case Permission::kEditAnyMessage:
            return "message.edit_any";
        case Permission::kViewAnyConversation:
            return "conversation.view_any";
        case Permission::kManageGroups:
            return "group.manage";
        case Permission::kBanUsers:
            return "user.ban";
        case Permission::kManageUsers:
            return "user.manage";
        case Permission::kManageRoles:
            return "user.manage_roles";
        case Permission::kViewSessions:
            return "session.view";
        case Permission::kRevokeSessions:
            return "session.revoke";
        case Permission::kViewAuditLogs:
            return "audit.view";
        case Permission::kViewSystemMetrics:
            return "system.metrics";
        case Permission::kManageFeatureFlags:
            return "system.feature_flags";
        case Permission::kCount:
            break;
    }
    return "unknown";
}

[[nodiscard]] constexpr std::optional<Permission> parse_permission(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kPermissionCount; ++i) {
        if (to_string(static_cast<Permission>(i)) == name) {
            return static_cast<Permission>(i);
        }
    }
    return std::nullopt;
}

namespace detail {

[[nodiscard]] constexpr std::uint32_t bit(Permission permission) noexcept {
    return static_cast<std::uint32_t>(1U) << static_cast<std::uint32_t>(permission);
}

// Permission grants, one bitmask per role, evaluated entirely at compile time.
//
// Each tier is written as "everything the tier below has, plus ...". That makes
// the hierarchy explicit and impossible to get subtly wrong, while still letting
// a future role deviate from strict inheritance if the product needs it.
inline constexpr std::uint32_t kUserGrants = 0U;  // ordinary users act only on their own data

inline constexpr std::uint32_t kModeratorGrants =
    kUserGrants | bit(Permission::kDeleteAnyMessage) | bit(Permission::kEditAnyMessage) |
    bit(Permission::kManageGroups);

inline constexpr std::uint32_t kAdminGrants =
    kModeratorGrants | bit(Permission::kViewAnyConversation) | bit(Permission::kBanUsers) |
    bit(Permission::kManageUsers) | bit(Permission::kViewSessions) |
    bit(Permission::kRevokeSessions) | bit(Permission::kViewAuditLogs) |
    bit(Permission::kViewSystemMetrics);

// Only the top tier may change roles or flip feature flags: both can be used to
// escalate privilege or disable safety controls, so they stay separated from
// day-to-day administration.
inline constexpr std::uint32_t kSuperAdminGrants =
    kAdminGrants | bit(Permission::kManageRoles) | bit(Permission::kManageFeatureFlags);

inline constexpr std::array<std::uint32_t, kRoleCount> kRoleGrants{
    kUserGrants,
    kModeratorGrants,
    kAdminGrants,
    kSuperAdminGrants,
};

}  // namespace detail

// The single authority on "may this role do this?". constexpr, so a static
// authorisation question costs nothing at run time.
[[nodiscard]] constexpr bool has_permission(Role role, Permission permission) noexcept {
    const auto index = static_cast<std::size_t>(role);
    if (index >= kRoleCount || permission == Permission::kCount) {
        return false;  // fail closed
    }
    return (detail::kRoleGrants[index] & detail::bit(permission)) != 0U;
}

// True when `role` is allowed to assign `target` — a super admin may grant any
// role, and nobody may grant a role at or above their own tier. Prevents an
// admin from minting another super admin, or from escalating themselves.
[[nodiscard]] constexpr bool can_assign_role(Role actor, Role target) noexcept {
    if (!has_permission(actor, Permission::kManageRoles)) {
        return false;
    }
    return static_cast<std::uint8_t>(target) <= static_cast<std::uint8_t>(actor);
}

// Sanity checks on the grant table, verified at compile time.
static_assert(!has_permission(Role::kUser, Permission::kDeleteAnyMessage));
static_assert(has_permission(Role::kModerator, Permission::kDeleteAnyMessage));
static_assert(!has_permission(Role::kModerator, Permission::kViewAuditLogs));
static_assert(has_permission(Role::kAdmin, Permission::kViewAuditLogs));
static_assert(!has_permission(Role::kAdmin, Permission::kManageRoles));
static_assert(has_permission(Role::kSuperAdmin, Permission::kManageRoles));
static_assert(can_assign_role(Role::kSuperAdmin, Role::kAdmin));
static_assert(!can_assign_role(Role::kAdmin, Role::kAdmin));

}  // namespace rtc::security
