#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

#include "rtc/docs/openapi.hpp"
#include "rtc/http/api_version.hpp"

namespace {

// Parses the compiled-in document once for the whole suite.
const nlohmann::json& spec() {
    static const nlohmann::json document = nlohmann::json::parse(rtc::docs::openapi_json());
    return document;
}

TEST(OpenApi, DocumentIsValidJson) {
    // The spec is a raw string literal, so a stray brace or an unescaped quote is
    // caught here rather than by a confused Swagger UI at runtime.
    const auto parsed = nlohmann::json::parse(rtc::docs::openapi_json(),
                                              nullptr,
                                              /*allow_exceptions=*/false);
    ASSERT_FALSE(parsed.is_discarded()) << "openapi_json() is not parseable JSON";
    EXPECT_TRUE(parsed.is_object());
}

TEST(OpenApi, DeclaresVersion31) {
    EXPECT_EQ(spec().at("openapi"), "3.1.0");
}

TEST(OpenApi, HasTheRequiredTopLevelSections) {
    for (const char* key : {"info", "servers", "tags", "components", "paths", "security"}) {
        EXPECT_TRUE(spec().contains(key)) << "missing " << key;
    }
    EXPECT_TRUE(spec().at("info").contains("title"));
    EXPECT_TRUE(spec().at("info").contains("version"));
}

TEST(OpenApi, DescribesBearerAuthentication) {
    const auto& scheme = spec().at("components").at("securitySchemes").at("bearerAuth");
    EXPECT_EQ(scheme.at("type"), "http");
    EXPECT_EQ(scheme.at("scheme"), "bearer");
    EXPECT_EQ(scheme.at("bearerFormat"), "JWT");
}

TEST(OpenApi, AppliesBearerAuthGloballyByDefault) {
    // Security should be opt-*out* per public route, not opt-in — forgetting to add
    // it to a new protected route would otherwise document it as public.
    ASSERT_TRUE(spec().at("security").is_array());
    ASSERT_FALSE(spec().at("security").empty());
    EXPECT_TRUE(spec().at("security").at(0).contains("bearerAuth"));
}

// This is the test that keeps the document honest.
//
// The route table is registered through a Crow macro that takes a compile-time
// string, so there is nothing to introspect at run time and the spec must be
// maintained by hand. Rather than trusting that, every route the controllers
// register is listed here, and each is asserted to be documented. Adding an
// endpoint without documenting it fails the build.
//
// Paths are given in the versioned form the spec uses, with Crow's `<int>` /
// `<string>` placeholders written as OpenAPI `{name}` templates.
TEST(OpenApi, DocumentsEveryRegisteredRoute) {
    const std::set<std::string> registered = {
        // Authentication
        "/api/v1/auth/register",
        "/api/v1/auth/login",
        "/api/v1/auth/refresh",
        "/api/v1/auth/logout",
        "/api/v1/auth/logout-all",
        "/api/v1/auth/me",
        // Users
        "/api/v1/users/me",
        "/api/v1/users/{id}",
        // Conversations
        "/api/v1/conversations",
        "/api/v1/conversations/{id}",
        "/api/v1/conversations/{id}/name",
        "/api/v1/conversations/{id}/members",
        "/api/v1/conversations/{id}/members/{user_id}",
        "/api/v1/conversations/{id}/leave",
        // Messages and reactions
        "/api/v1/messages",
        "/api/v1/messages/{id}",
        "/api/v1/messages/{id}/reactions",
        // Attachments
        "/api/v1/attachments",
        "/api/v1/attachments/{id}",
        "/api/v1/attachments/{id}/thumbnail",
        // Notifications
        "/api/v1/notifications",
        "/api/v1/notifications/{id}",
        "/api/v1/notifications/{id}/read",
        "/api/v1/notifications/read-all",
        // Sessions
        "/api/v1/sessions",
        "/api/v1/sessions/{session_id}",
        // Search
        "/api/v1/search/messages",
        // Admin
        "/api/v1/admin/users",
        "/api/v1/admin/users/{id}",
        "/api/v1/admin/users/{id}/role",
        "/api/v1/admin/users/{id}/ban",
        "/api/v1/admin/users/{id}/unban",
        "/api/v1/admin/users/{id}/sessions",
        "/api/v1/admin/conversations/{id}",
        "/api/v1/admin/websockets",
        "/api/v1/admin/cache",
        "/api/v1/admin/jobs",
        "/api/v1/admin/system",
        "/api/v1/admin/audit-logs",
        "/api/v1/admin/audit-logs/summary",
        "/api/v1/admin/features",
        "/api/v1/admin/features/{name}",
        // Operations
        "/health",
        "/health/live",
        "/health/ready",
        "/health/startup",
        "/metrics",
    };

    const auto& paths = spec().at("paths");
    for (const std::string& path : registered) {
        EXPECT_TRUE(paths.contains(path))
            << "route is not documented in the OpenAPI spec: " << path;
    }
}

TEST(OpenApi, EveryDocumentedPathHasAtLeastOneOperation) {
    static const std::set<std::string> kMethods = {
        "get", "post", "put", "patch", "delete", "head", "options"};
    for (const auto& [path, item] : spec().at("paths").items()) {
        bool has_operation = false;
        for (const auto& [key, _] : item.items()) {
            if (kMethods.count(key) != 0U) {
                has_operation = true;
                break;
            }
        }
        EXPECT_TRUE(has_operation) << path << " declares no HTTP operation";
    }
}

TEST(OpenApi, EveryOperationDeclaresResponsesAndATag) {
    static const std::set<std::string> kMethods = {"get", "post", "put", "patch", "delete"};
    for (const auto& [path, item] : spec().at("paths").items()) {
        for (const auto& [method, operation] : item.items()) {
            if (kMethods.count(method) == 0U) {
                continue;
            }
            EXPECT_TRUE(operation.contains("responses"))
                << path << " " << method << " declares no responses";
            EXPECT_TRUE(operation.contains("summary"))
                << path << " " << method << " has no summary";
            EXPECT_TRUE(operation.contains("tags")) << path << " " << method << " has no tag";
        }
    }
}

TEST(OpenApi, OperationIdsAreUnique) {
    // Duplicate operationIds break generated clients.
    static const std::set<std::string> kMethods = {"get", "post", "put", "patch", "delete"};
    std::set<std::string> seen;
    for (const auto& [path, item] : spec().at("paths").items()) {
        for (const auto& [method, operation] : item.items()) {
            if (kMethods.count(method) == 0U || !operation.contains("operationId")) {
                continue;
            }
            const auto id = operation.at("operationId").get<std::string>();
            EXPECT_TRUE(seen.insert(id).second) << "duplicate operationId: " << id;
        }
    }
}

TEST(OpenApi, DocumentsTheCanonicalErrorEnvelope) {
    const auto& schema = spec().at("components").at("schemas").at("Error");
    EXPECT_TRUE(schema.at("properties").contains("error"));
    const auto& codes = schema.at("properties").at("error").at("properties").at("code").at("enum");
    // Each of these is produced by rtc::errors::code_for or the version middleware.
    for (const char* code : {"validation_error",
                             "authentication_error",
                             "authorization_error",
                             "not_found",
                             "conflict",
                             "rate_limited",
                             "internal_error",
                             "unsupported_api_version"}) {
        bool found = false;
        for (const auto& entry : codes) {
            if (entry == code) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "error code not documented: " << code;
    }
}

TEST(OpenApi, EveryComponentRefResolves) {
    // A dangling $ref renders as a broken schema in the viewer and breaks codegen.
    // Walk the document and check every local reference points at something real.
    std::function<void(const nlohmann::json&)> check = [&](const nlohmann::json& node) {
        if (node.is_object()) {
            for (const auto& [key, value] : node.items()) {
                if (key == "$ref" && value.is_string()) {
                    const auto ref = value.get<std::string>();
                    ASSERT_EQ(ref.rfind("#/", 0), 0U) << "non-local $ref: " << ref;
                    const nlohmann::json* cursor = &spec();
                    std::size_t start = 2;
                    while (start <= ref.size()) {
                        const std::size_t slash = ref.find('/', start);
                        const std::string token = ref.substr(
                            start, slash == std::string::npos ? std::string::npos : slash - start);
                        ASSERT_TRUE(cursor->contains(token)) << "unresolved $ref: " << ref;
                        cursor = &cursor->at(token);
                        if (slash == std::string::npos) {
                            break;
                        }
                        start = slash + 1;
                    }
                } else {
                    check(value);
                }
            }
        } else if (node.is_array()) {
            for (const auto& element : node) {
                check(element);
            }
        }
    };
    check(spec());
}

TEST(OpenApi, ServerUrlIsRewrittenForTheCurrentDeployment) {
    // Swagger UI's "Try it out" must target the host being viewed, not a hardcoded
    // localhost.
    const auto patched =
        nlohmann::json::parse(rtc::docs::openapi_json_for("https://chat.example.com"));
    ASSERT_TRUE(patched.at("servers").is_array());
    EXPECT_EQ(patched.at("servers").at(0).at("url"), "https://chat.example.com");
    // Patching must not damage the rest of the document.
    EXPECT_TRUE(patched.contains("paths"));
    EXPECT_EQ(patched.at("openapi"), "3.1.0");
}

TEST(OpenApi, EmptyBaseUrlLeavesTheDocumentUnchanged) {
    EXPECT_EQ(rtc::docs::openapi_json_for(""), std::string(rtc::docs::openapi_json()));
}

TEST(OpenApi, SwaggerUiPageReferencesTheSpecAndIsSelfContained) {
    const std::string html = rtc::docs::swagger_ui_html("/openapi.json");
    EXPECT_NE(html.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(html.find("/openapi.json"), std::string::npos);
    EXPECT_NE(html.find("SwaggerUIBundle"), std::string::npos);
    // Assets must be pinned, never floating on "latest".
    EXPECT_EQ(html.find("swagger-ui-dist@latest"), std::string::npos);
}

TEST(OpenApi, SwaggerUiCspStillForbidsFraming) {
    const std::string csp(rtc::docs::swagger_ui_csp());
    EXPECT_NE(csp.find("frame-ancestors 'none'"), std::string::npos);
    EXPECT_NE(csp.find("default-src 'none'"), std::string::npos);
    // The relaxation is limited to the pinned CDN.
    EXPECT_NE(csp.find("https://cdn.jsdelivr.net"), std::string::npos);
}

TEST(OpenApi, DocumentedVersionMatchesTheServedVersion) {
    // The spec documents "/api/v1/..." paths, so v1 must actually be supported.
    EXPECT_TRUE(rtc::http::is_supported_api_version(1));
    const auto& paths = spec().at("paths");
    EXPECT_TRUE(paths.contains("/api/v1/auth/login"));
}

}  // namespace
