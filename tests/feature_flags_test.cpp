#include "rtc/features/feature_flags.hpp"

#include <gtest/gtest.h>

#include <string>

#include "rtc/errors/exceptions.hpp"
#include "support/env_guard.hpp"

namespace {

using rtc::features::Feature;
using rtc::features::FeatureFlags;

TEST(FeatureFlags, DefaultsMatchTheRegistry) {
    // Every capability the service already shipped defaults on, so introducing
    // flags changes no existing behaviour.
    const FeatureFlags flags;
    for (const auto& entry : rtc::features::kFeatureRegistry) {
        EXPECT_EQ(flags.is_enabled(entry.feature), entry.default_enabled) << entry.name;
    }
}

TEST(FeatureFlags, EverythingIsEnabledByDefault) {
    const FeatureFlags flags;
    EXPECT_TRUE(flags.is_enabled(Feature::kReactions));
    EXPECT_TRUE(flags.is_enabled(Feature::kUploads));
    EXPECT_TRUE(flags.is_enabled(Feature::kNotifications));
    EXPECT_TRUE(flags.is_enabled(Feature::kTyping));
    EXPECT_TRUE(flags.is_enabled(Feature::kSearch));
    EXPECT_TRUE(flags.is_enabled(Feature::kAuditLog));
}

TEST(FeatureFlags, SetReturnsThePreviousValue) {
    FeatureFlags flags;
    EXPECT_TRUE(flags.set(Feature::kReactions, false));
    EXPECT_FALSE(flags.is_enabled(Feature::kReactions));
    EXPECT_FALSE(flags.set(Feature::kReactions, true));
    EXPECT_TRUE(flags.is_enabled(Feature::kReactions));
}

TEST(FeatureFlags, RequireThrowsWhenDisabled) {
    FeatureFlags flags;
    EXPECT_NO_THROW(flags.require(Feature::kSearch));

    flags.set(Feature::kSearch, false);
    EXPECT_THROW(flags.require(Feature::kSearch), rtc::features::FeatureDisabledException);
}

TEST(FeatureFlags, DisabledFeatureLooksAbsentRatherThanForbidden) {
    // 404, not 403: telling a caller the endpoint exists but is forbidden would be
    // misleading when the capability is simply switched off.
    FeatureFlags flags;
    flags.set(Feature::kUploads, false);
    try {
        flags.require(Feature::kUploads);
        FAIL() << "expected FeatureDisabledException";
    } catch (const rtc::errors::AppException& ex) {
        EXPECT_EQ(ex.http_status(), 404);
        EXPECT_EQ(ex.details(), "feature=uploads");
    }
}

TEST(FeatureFlags, ParsesFlagNames) {
    const auto parsed = rtc::features::parse_feature("reactions");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, Feature::kReactions);

    EXPECT_FALSE(rtc::features::parse_feature("nonexistent").has_value());
    EXPECT_FALSE(rtc::features::parse_feature("").has_value());
    EXPECT_FALSE(rtc::features::parse_feature("Reactions").has_value());  // case-sensitive
}

TEST(FeatureFlags, RegistryNamesAreUniqueAndNonEmpty) {
    // The admin API addresses flags by name, so a duplicate would make one
    // unreachable.
    for (std::size_t a = 0; a < rtc::features::kFeatureCount; ++a) {
        EXPECT_FALSE(rtc::features::kFeatureRegistry[a].name.empty());
        EXPECT_FALSE(rtc::features::kFeatureRegistry[a].env_var.empty());
        for (std::size_t b = a + 1; b < rtc::features::kFeatureCount; ++b) {
            EXPECT_NE(rtc::features::kFeatureRegistry[a].name,
                      rtc::features::kFeatureRegistry[b].name);
            EXPECT_NE(rtc::features::kFeatureRegistry[a].env_var,
                      rtc::features::kFeatureRegistry[b].env_var);
        }
    }
}

TEST(FeatureFlags, RegistryIndicesMatchTheirEnumValues) {
    // descriptor() indexes the registry by enum ordinal; a misordered entry would
    // silently return the wrong flag's metadata.
    for (std::size_t i = 0; i < rtc::features::kFeatureCount; ++i) {
        EXPECT_EQ(static_cast<std::size_t>(rtc::features::kFeatureRegistry[i].feature), i);
    }
}

TEST(FeatureFlags, ReadsTruthyEnvironmentValues) {
    for (const char* truthy : {"1", "true", "TRUE", "yes", "on", "On"}) {
        const rtc::testing::EnvGuard guard("ENABLE_REACTIONS", truthy);
        FeatureFlags flags;
        flags.set(Feature::kReactions, false);
        flags.load_from_env();
        EXPECT_TRUE(flags.is_enabled(Feature::kReactions)) << truthy;
    }
}

TEST(FeatureFlags, ReadsFalseyEnvironmentValues) {
    for (const char* falsey : {"0", "false", "no", "off", "nonsense"}) {
        const rtc::testing::EnvGuard guard("ENABLE_REACTIONS", falsey);
        FeatureFlags flags;
        flags.load_from_env();
        EXPECT_FALSE(flags.is_enabled(Feature::kReactions)) << falsey;
    }
}

TEST(FeatureFlags, UnsetEnvironmentVariableKeepsTheDefault) {
    const rtc::testing::EnvGuard guard("ENABLE_SEARCH");  // unset for this scope
    FeatureFlags flags;
    flags.load_from_env();
    EXPECT_TRUE(flags.is_enabled(Feature::kSearch));
}

TEST(FeatureFlags, SerialisesToJsonForTheAdminApi) {
    FeatureFlags flags;
    flags.set(Feature::kTyping, false);
    const auto json = flags.to_json();

    ASSERT_TRUE(json.is_array());
    EXPECT_EQ(json.size(), rtc::features::kFeatureCount);

    bool found_typing = false;
    for (const auto& entry : json) {
        EXPECT_TRUE(entry.contains("name"));
        EXPECT_TRUE(entry.contains("enabled"));
        EXPECT_TRUE(entry.contains("env"));
        EXPECT_TRUE(entry.contains("description"));
        if (entry.at("name") == "typing") {
            found_typing = true;
            EXPECT_FALSE(entry.at("enabled").get<bool>());
            EXPECT_EQ(entry.at("env"), "ENABLE_TYPING");
        }
    }
    EXPECT_TRUE(found_typing);
}

TEST(FeatureFlags, ListsEnabledNames) {
    FeatureFlags flags;
    const auto all = flags.enabled_names();
    EXPECT_EQ(all.size(), rtc::features::kFeatureCount);

    flags.set(Feature::kSearch, false);
    EXPECT_EQ(flags.enabled_names().size(), rtc::features::kFeatureCount - 1);
}

}  // namespace
