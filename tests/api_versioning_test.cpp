#include <gtest/gtest.h>

#include "rtc/http/api_version.hpp"
#include "rtc/middlewares/versioning_middleware.hpp"

namespace {

using rtc::middlewares::parse_versioned_path;

// --- paths that carry a version prefix -------------------------------------

TEST(ApiVersioning, RewritesVersionedPathToUnversioned) {
    const auto result = parse_versioned_path("/api/v1/messages");
    EXPECT_TRUE(result.has_prefix);
    EXPECT_EQ(result.version, 1);
    EXPECT_EQ(result.normalised, "/api/messages");
}

TEST(ApiVersioning, RewritesNestedPath) {
    const auto result = parse_versioned_path("/api/v1/conversations/42/members/7");
    EXPECT_TRUE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/api/conversations/42/members/7");
}

TEST(ApiVersioning, HandlesBarePrefixWithNoTrailingSegment) {
    const auto result = parse_versioned_path("/api/v1");
    EXPECT_TRUE(result.has_prefix);
    EXPECT_EQ(result.version, 1);
    EXPECT_EQ(result.normalised, "/api");
}

TEST(ApiVersioning, PreservesQueryString) {
    // req.url normally excludes the query string, but the rewrite must not corrup
    // one if it is ever present.
    const auto result = parse_versioned_path("/api/v1/messages?limit=10&q=hi");
    EXPECT_TRUE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/api/messages?limit=10&q=hi");
}

TEST(ApiVersioning, ParsesMultiDigitAndFutureVersions) {
    const auto result = parse_versioned_path("/api/v12/users");
    EXPECT_TRUE(result.has_prefix);
    EXPECT_EQ(result.version, 12);
    // Recognised as a version prefix even though unsupported — that distinction is
    // what lets the middleware answer 404 with a helpful message instead of a
    // generic route miss.
    EXPECT_FALSE(rtc::http::is_supported_api_version(result.version));
}

// --- paths that must be left completely alone ------------------------------

TEST(ApiVersioning, LeavesUnversionedApiPathUntouched) {
    const auto result = parse_versioned_path("/api/messages");
    EXPECT_FALSE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/api/messages");
}

TEST(ApiVersioning, DoesNotTreatWordStartingWithVAsAVersion) {
    // "/api/version" begins with "/api/v" but has no digits after it.
    const auto result = parse_versioned_path("/api/version");
    EXPECT_FALSE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/api/version");
}

TEST(ApiVersioning, RequiresTheVersionSegmentToEndAtASeparator) {
    // "v1x" is not a version segment; treating it as one would silently route
    // "/api/v1x/foo" to "/api/foo".
    const auto result = parse_versioned_path("/api/v1x/foo");
    EXPECT_FALSE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/api/v1x/foo");
}

TEST(ApiVersioning, IgnoresOperationalPaths) {
    for (const char* path : {"/health", "/health/ready", "/metrics", "/docs", "/openapi.json"}) {
        const auto result = parse_versioned_path(path);
        EXPECT_FALSE(result.has_prefix) << path;
        EXPECT_EQ(result.normalised, path);
    }
}

TEST(ApiVersioning, IgnoresPathsOutsideApiNamespace) {
    const auto result = parse_versioned_path("/v1/messages");
    EXPECT_FALSE(result.has_prefix);
    EXPECT_EQ(result.normalised, "/v1/messages");
}

TEST(ApiVersioning, HandlesEmptyAndShortPaths) {
    EXPECT_FALSE(parse_versioned_path("").has_prefix);
    EXPECT_FALSE(parse_versioned_path("/").has_prefix);
    EXPECT_FALSE(parse_versioned_path("/api").has_prefix);
    EXPECT_FALSE(parse_versioned_path("/api/v").has_prefix);
}

// --- version policy -------------------------------------------------------

TEST(ApiVersioning, PolicyConstantsAreConsistent) {
    EXPECT_TRUE(rtc::http::is_supported_api_version(rtc::http::kDefaultApiVersion));
    EXPECT_TRUE(rtc::http::is_supported_api_version(rtc::http::kCurrentApiVersion));
    EXPECT_FALSE(rtc::http::is_supported_api_version(0));
    EXPECT_FALSE(rtc::http::is_supported_api_version(99));
}

TEST(ApiVersioning, DefaultVersionIsOneForBackwardCompatibility) {
    // The unversioned "/api" prefix is an alias for this version. Changing it
    // would silently move every legacy client onto a different contract, so the
    // value is pinned by a test rather than by convention.
    EXPECT_EQ(rtc::http::kDefaultApiVersion, 1);
}

TEST(ApiVersioning, RendersLabels) {
    EXPECT_EQ(rtc::http::api_version_label(1), "v1");
    EXPECT_EQ(rtc::http::api_version_label(2), "v2");
    EXPECT_EQ(rtc::http::supported_api_versions_label(), "v1");
}

}  // namespace
