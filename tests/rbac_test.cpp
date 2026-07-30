#include <gtest/gtest.h>

#include "rtc/security/role.hpp"

namespace {

using rtc::security::can_assign_role;
using rtc::security::has_permission;
using rtc::security::parse_permission;
using rtc::security::parse_role;
using rtc::security::Permission;
using rtc::security::Role;

// --- role names ------------------------------------------------------------

TEST(Rbac, RoleNamesRoundTrip) {
    for (const Role role : {Role::kUser, Role::kModerator, Role::kAdmin, Role::kSuperAdmin}) {
        const auto parsed = parse_role(rtc::security::to_string(role));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, role);
    }
}

TEST(Rbac, UnknownRoleNameIsRejected) {
    // Returning nullopt rather than defaulting is what lets the persistence layer
    // decide to fail closed; a silent default here would hide bad data.
    EXPECT_FALSE(parse_role("root").has_value());
    EXPECT_FALSE(parse_role("").has_value());
    EXPECT_FALSE(parse_role("Admin").has_value());  // case-sensitive by design
}

TEST(Rbac, PermissionNamesRoundTrip) {
    for (std::size_t i = 0; i < rtc::security::kPermissionCount; ++i) {
        const auto permission = static_cast<Permission>(i);
        const auto parsed = parse_permission(rtc::security::to_string(permission));
        ASSERT_TRUE(parsed.has_value()) << rtc::security::to_string(permission);
        EXPECT_EQ(*parsed, permission);
    }
}

// --- the grant table -------------------------------------------------------

TEST(Rbac, OrdinaryUserHoldsNoElevatedPermission) {
    // A plain user acts only on their own data; every listed capability is about
    // acting on someone else's.
    for (std::size_t i = 0; i < rtc::security::kPermissionCount; ++i) {
        EXPECT_FALSE(has_permission(Role::kUser, static_cast<Permission>(i)))
            << rtc::security::to_string(static_cast<Permission>(i));
    }
}

TEST(Rbac, ModeratorCanModerateContentButNotAdminister) {
    EXPECT_TRUE(has_permission(Role::kModerator, Permission::kDeleteAnyMessage));
    EXPECT_TRUE(has_permission(Role::kModerator, Permission::kEditAnyMessage));
    EXPECT_TRUE(has_permission(Role::kModerator, Permission::kManageGroups));

    EXPECT_FALSE(has_permission(Role::kModerator, Permission::kBanUsers));
    EXPECT_FALSE(has_permission(Role::kModerator, Permission::kViewAuditLogs));
    EXPECT_FALSE(has_permission(Role::kModerator, Permission::kManageRoles));
}

TEST(Rbac, AdminCanAdministerButNotChangeRolesOrFlags) {
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kBanUsers));
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kManageUsers));
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kViewAuditLogs));
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kViewSessions));
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kRevokeSessions));
    EXPECT_TRUE(has_permission(Role::kAdmin, Permission::kViewSystemMetrics));

    // Both are privilege-escalation vectors and are reserved for the top tier.
    EXPECT_FALSE(has_permission(Role::kAdmin, Permission::kManageRoles));
    EXPECT_FALSE(has_permission(Role::kAdmin, Permission::kManageFeatureFlags));
}

TEST(Rbac, SuperAdminHoldsEveryPermission) {
    for (std::size_t i = 0; i < rtc::security::kPermissionCount; ++i) {
        EXPECT_TRUE(has_permission(Role::kSuperAdmin, static_cast<Permission>(i)))
            << rtc::security::to_string(static_cast<Permission>(i));
    }
}

TEST(Rbac, GrantsAreCumulativeUpTheHierarchy) {
    // Every permission a lower tier holds must also be held by each higher tier.
    // Asserting the property directly means a future edit to one bitmask cannot
    // silently break inheritance.
    const Role ordered[] = {Role::kUser, Role::kModerator, Role::kAdmin, Role::kSuperAdmin};
    for (std::size_t lower = 0; lower + 1 < std::size(ordered); ++lower) {
        for (std::size_t i = 0; i < rtc::security::kPermissionCount; ++i) {
            const auto permission = static_cast<Permission>(i);
            if (has_permission(ordered[lower], permission)) {
                EXPECT_TRUE(has_permission(ordered[lower + 1], permission))
                    << "role " << rtc::security::to_string(ordered[lower + 1])
                    << " is missing " << rtc::security::to_string(permission);
            }
        }
    }
}

TEST(Rbac, FailsClosedOnAnOutOfRangeRole) {
    // Defensive: a corrupt value must never be read as a grant.
    const auto bogus = static_cast<Role>(200);
    EXPECT_FALSE(has_permission(bogus, Permission::kBanUsers));
    EXPECT_FALSE(has_permission(bogus, Permission::kManageRoles));
}

// --- role assignment guard -------------------------------------------------

TEST(Rbac, OnlyRoleManagersMayAssignRoles) {
    EXPECT_FALSE(can_assign_role(Role::kUser, Role::kUser));
    EXPECT_FALSE(can_assign_role(Role::kModerator, Role::kUser));
    EXPECT_FALSE(can_assign_role(Role::kAdmin, Role::kUser));
    EXPECT_TRUE(can_assign_role(Role::kSuperAdmin, Role::kUser));
}

TEST(Rbac, NobodyMayGrantARoleAtOrAboveTheirOwnTier) {
    // A super admin may create admins and below, but the guard still refuses a
    // self-equal grant for any lesser role — this is the escalation defence.
    EXPECT_TRUE(can_assign_role(Role::kSuperAdmin, Role::kAdmin));
    EXPECT_TRUE(can_assign_role(Role::kSuperAdmin, Role::kModerator));
    EXPECT_TRUE(can_assign_role(Role::kSuperAdmin, Role::kSuperAdmin));

    EXPECT_FALSE(can_assign_role(Role::kAdmin, Role::kAdmin));
    EXPECT_FALSE(can_assign_role(Role::kAdmin, Role::kSuperAdmin));
}

TEST(Rbac, PermissionBitmaskHasRoomToGrow) {
    // The grant table is a uint32_t; this guards the invariant the static_assert
    // in role.hpp also checks, so the failure is visible in the test report too.
    EXPECT_LE(rtc::security::kPermissionCount, 32U);
}

}  // namespace
