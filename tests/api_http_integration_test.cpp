#include <crow/common.h>
#include <crow/http_request.h>
#include <crow/http_response.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "rtc/http/api_version.hpp"
#include "rtc/models/message.hpp"
#include "rtc/models/reaction.hpp"
#include "support/test_api_app.hpp"

// HTTP-level regression coverage for API versioning and the end-to-end request
// flow.
//
// Why this file exists
// --------------------
// The suite that shipped before this covered parse_versioned_path() — the pure
// path-parsing helper — and nothing else about versioning. That helper was
// correct, and every one of its tests passed, while every "/api/v1/..." request
// the service actually received returned 404. The defect was never in the
// parsing; it was that the parsed result was applied in a middleware, after Crow
// had already resolved (and failed to resolve) the route.
//
// A unit test of a helper cannot see that. These tests drive requests through
// crow::App::handle_full(), which calls handle_initial() to route and then
// handle() to run the middleware chain and handler — the same two steps, in the
// same order, that crow::http_connection performs for a live socket. Anything
// that breaks routing therefore breaks these tests.

namespace {

using nlohmann::json;
using rtc::testing::TestApiApp;

// Every API path is served under both prefixes. Parity is asserted rather than
// assumed: an endpoint reachable at one prefix and not the other is exactly the
// bug this file exists to prevent, and it can be reintroduced one route at a
// time by a controller that uses CROW_ROUTE instead of RTC_API_ROUTE.
struct RouteCase {
    crow::HTTPMethod method;
    const char* suffix;  // path after the API prefix
    const char* body;    // empty => no body
};

const std::vector<RouteCase>& route_matrix() {
    static const std::vector<RouteCase> cases = {
        {crow::HTTPMethod::Post,
         "/auth/register",
         R"({"username":"parity_user","email":"parity@example.com","password":"correct-horse-battery"})"},
        {crow::HTTPMethod::Post,
         "/auth/login",
         R"({"identifier":"nobody","password":"correct-horse-battery"})"},
        {crow::HTTPMethod::Post, "/auth/refresh", R"({"refresh_token":"x","session_id":"y"})"},
        {crow::HTTPMethod::Post, "/auth/logout", R"({"session_id":"y"})"},
        {crow::HTTPMethod::Post, "/auth/logout-all", ""},
        {crow::HTTPMethod::Get, "/auth/me", ""},
        {crow::HTTPMethod::Get, "/users/me", ""},
        {crow::HTTPMethod::Get, "/users/1", ""},
        {crow::HTTPMethod::Get, "/conversations", ""},
        {crow::HTTPMethod::Post, "/conversations", R"({"type":"direct","participant_ids":[2]})"},
        {crow::HTTPMethod::Get, "/conversations/1", ""},
        {crow::HTTPMethod::Patch, "/conversations/1/name", R"({"name":"x"})"},
        {crow::HTTPMethod::Post, "/conversations/1/members", R"({"user_id":2})"},
        {crow::HTTPMethod::Post, "/conversations/1/leave", ""},
        {crow::HTTPMethod::Get, "/messages", ""},
        {crow::HTTPMethod::Post, "/messages", R"({"conversation_id":1,"content":"hi"})"},
        {crow::HTTPMethod::Patch, "/messages/1", R"({"content":"edited"})"},
        {crow::HTTPMethod::Delete, "/messages/1", ""},
        {crow::HTTPMethod::Get, "/messages/1/reactions", ""},
        {crow::HTTPMethod::Get, "/sessions", ""},
        {crow::HTTPMethod::Get, "/search/messages?q=hello", ""},
    };
    return cases;
}

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

TEST(ApiVersioningHttp, VersionedPrefixIsRoutedAtAll) {
    // The narrowest statement of the original bug: this exact request returned
    // 404 because no rule matched "/api/v1/auth/register".
    TestApiApp app;
    const auto res = app.request(
        crow::HTTPMethod::Post,
        "/api/v1/auth/register",
        R"({"username":"ada","email":"ada@example.com","password":"correct-horse-battery"})");
    EXPECT_EQ(res.code, 201) << "body: " << res.body;
}

TEST(ApiVersioningHttp, BothPrefixesAcceptRegistration) {
    TestApiApp app;
    const auto legacy = app.request(
        crow::HTTPMethod::Post,
        "/api/auth/register",
        R"({"username":"legacy","email":"legacy@example.com","password":"correct-horse-battery"})");
    const auto versioned = app.request(
        crow::HTTPMethod::Post,
        "/api/v1/auth/register",
        R"({"username":"versioned","email":"versioned@example.com","password":"correct-horse-battery"})");
    EXPECT_EQ(legacy.code, 201) << legacy.body;
    EXPECT_EQ(versioned.code, 201) << versioned.body;
}

TEST(ApiVersioningHttp, BothPrefixesAcceptLogin) {
    TestApiApp app;
    ASSERT_FALSE(app.register_user("ada", "ada@example.com").empty());

    const std::string credentials = R"({"identifier":"ada","password":"correct-horse-battery"})";
    const auto legacy = app.request(crow::HTTPMethod::Post, "/api/auth/login", credentials);
    const auto versioned = app.request(crow::HTTPMethod::Post, "/api/v1/auth/login", credentials);
    EXPECT_EQ(legacy.code, 200) << legacy.body;
    EXPECT_EQ(versioned.code, 200) << versioned.body;
}

TEST(ApiVersioningHttp, EveryRouteBehavesIdenticallyUnderBothPrefixes) {
    for (const auto& route : route_matrix()) {
        // A fresh app per case so state written by one prefix cannot change the
        // status the other receives (registration conflicts, for instance).
        TestApiApp legacy_app;
        TestApiApp versioned_app;
        const std::string body = route.body;

        const auto legacy =
            legacy_app.request(route.method, std::string("/api") + route.suffix, body);
        const auto versioned =
            versioned_app.request(route.method, std::string("/api/v1") + route.suffix, body);

        EXPECT_EQ(legacy.code, versioned.code)
            << "prefix parity broken for " << route.suffix << "\n  /api      -> " << legacy.code
            << " " << legacy.body << "\n  /api/v1   -> " << versioned.code << " " << versioned.body;

        // 404 on either side means the route table is missing the path — the
        // precise failure mode this suite guards. Guarded separately from the
        // equality check above, which a symmetric 404 would happily satisfy.
        EXPECT_NE(versioned.code, 404) << "no route registered for /api/v1" << route.suffix;
        EXPECT_NE(legacy.code, 404) << "no route registered for /api" << route.suffix;
    }
}

// X-API-Version stamping and the unsupported-version rejection are the
// responsibility of ApiVersionMiddleware, which Crow runs from http_connection
// rather than from the app object. handle_full() therefore cannot observe them —
// they are covered over a real socket in http_server_live_test.cpp.

TEST(ApiVersioningHttp, UnknownPathUnderASupportedVersionIsACanonicalNotFound) {
    TestApiApp app;
    const auto res = app.request(crow::HTTPMethod::Get, "/api/v1/no-such-endpoint");
    ASSERT_EQ(res.code, 404);
    const auto body = TestApiApp::body_of(res);
    ASSERT_FALSE(body.is_discarded()) << res.body;
    EXPECT_EQ(body.at("error").at("code"), "not_found");
}

TEST(ApiVersioningHttp, OperationalEndpointsStayUnversioned) {
    // Health and metrics are deliberately outside the /api namespace: probes
    // should not have to track API versions.
    TestApiApp app;
    EXPECT_EQ(app.request(crow::HTTPMethod::Get, "/health").code, 200);
    EXPECT_EQ(app.request(crow::HTTPMethod::Get, "/health/live").code, 200);
}

// ---------------------------------------------------------------------------
// End-to-end smoke flow
// ---------------------------------------------------------------------------

// Walks the documented happy path in one sequence, over the versioned prefix,
// asserting each step against the contract the OpenAPI document publishes.
TEST(ApiSmoke, FullMessagingFlowOverTheVersionedPrefix) {
    TestApiApp app;

    // --- Register -----------------------------------------------------------
    const auto registered = app.request(
        crow::HTTPMethod::Post,
        "/api/v1/auth/register",
        R"({"username":"ada","email":"ada@example.com","password":"correct-horse-battery"})");
    ASSERT_EQ(registered.code, 201) << registered.body;
    const auto registered_body = TestApiApp::body_of(registered);
    ASSERT_FALSE(registered_body.is_discarded());
    EXPECT_TRUE(registered_body.contains("session_id"));

    const auto peer = app.request(
        crow::HTTPMethod::Post,
        "/api/v1/auth/register",
        R"({"username":"bob","email":"bob@example.com","password":"correct-horse-battery"})");
    ASSERT_EQ(peer.code, 201) << peer.body;
    const auto peer_id = TestApiApp::body_of(peer).at("user").at("id").get<std::int64_t>();

    // --- Login --------------------------------------------------------------
    const auto login = app.request(crow::HTTPMethod::Post,
                                   "/api/v1/auth/login",
                                   R"({"identifier":"ada","password":"correct-horse-battery"})");
    ASSERT_EQ(login.code, 200) << login.body;
    const auto login_body = TestApiApp::body_of(login);
    const auto token = login_body.at("tokens").at("access_token").get<std::string>();
    const auto session_id = login_body.at("session_id").get<std::string>();
    ASSERT_FALSE(token.empty());

    // --- JWT authentication -------------------------------------------------
    EXPECT_EQ(app.request(crow::HTTPMethod::Get, "/api/v1/auth/me").code, 401)
        << "protected route must reject an anonymous caller";
    const auto me = app.request(crow::HTTPMethod::Get, "/api/v1/auth/me", "", token);
    ASSERT_EQ(me.code, 200) << me.body;
    EXPECT_EQ(TestApiApp::body_of(me).at("username"), "ada");

    // --- Create conversation ------------------------------------------------
    // Exactly the payload the OpenAPI example publishes.
    const auto created =
        app.request(crow::HTTPMethod::Post,
                    "/api/v1/conversations",
                    json{{"type", "direct"}, {"participant_ids", {peer_id}}}.dump(),
                    token);
    ASSERT_EQ(created.code, 201) << created.body;
    const auto conversation = TestApiApp::body_of(created);
    const auto conversation_id = conversation.at("id").get<std::int64_t>();
    EXPECT_EQ(conversation.at("type"), "direct");
    EXPECT_TRUE(conversation.contains("participants"))
        << "response shape drifted from the documented Conversation schema";

    // --- Send message -------------------------------------------------------
    const auto sent = app.request(
        crow::HTTPMethod::Post,
        "/api/v1/messages",
        json{{"conversation_id", conversation_id}, {"content", "Hello from the smoke test"}}.dump(),
        token);
    ASSERT_EQ(sent.code, 201) << sent.body;
    const auto message = TestApiApp::body_of(sent);
    const auto message_id = message.at("id").get<std::int64_t>();
    EXPECT_EQ(message.at("content"), "Hello from the smoke test");
    EXPECT_FALSE(message.at("deleted").get<bool>());
    EXPECT_FALSE(message.at("edited").get<bool>());

    // --- List ---------------------------------------------------------------
    const auto listed =
        app.request(crow::HTTPMethod::Get,
                    "/api/v1/messages?conversation_id=" + std::to_string(conversation_id),
                    "",
                    token);
    ASSERT_EQ(listed.code, 200) << listed.body;
    EXPECT_EQ(TestApiApp::body_of(listed).at("messages").size(), 1U);

    // --- Edit message -------------------------------------------------------
    const auto edited = app.request(crow::HTTPMethod::Patch,
                                    "/api/v1/messages/" + std::to_string(message_id),
                                    R"({"content":"Edited by the smoke test"})",
                                    token);
    ASSERT_EQ(edited.code, 200) << edited.body;
    const auto edited_body = TestApiApp::body_of(edited);
    EXPECT_EQ(edited_body.at("content"), "Edited by the smoke test");
    EXPECT_TRUE(edited_body.at("edited").get<bool>());

    // --- Add reaction -------------------------------------------------------
    // Drawn from the allow-list itself, so the test cannot drift from the enum
    // the OpenAPI document publishes.
    const std::string emoji{rtc::models::kAllowedReactions.front()};
    const auto reacted =
        app.request(crow::HTTPMethod::Post,
                    "/api/v1/messages/" + std::to_string(message_id) + "/reactions",
                    json{{"emoji", emoji}}.dump(),
                    token);
    ASSERT_EQ(reacted.code, 201) << reacted.body;
    EXPECT_EQ(TestApiApp::body_of(reacted).at("emoji"), emoji);

    const auto rejected =
        app.request(crow::HTTPMethod::Post,
                    "/api/v1/messages/" + std::to_string(message_id) + "/reactions",
                    R"({"emoji":"thumbsup"})",
                    token);
    EXPECT_EQ(rejected.code, 400)
        << "a shortcode is not in the documented enum and must be refused";

    // --- Search -------------------------------------------------------------
    rtc::models::Message hit_message;
    hit_message.id = message_id;
    hit_message.conversation_id = conversation_id;
    hit_message.sender_id = 1;
    hit_message.content = "Edited by the smoke test";
    app.search_repository().hits.push_back(rtc::repositories::MessageSearchHit{
        .message = hit_message, .rank = 1.0, .headline = "Edited", .fuzzy_match = false});
    const auto search =
        app.request(crow::HTTPMethod::Get, "/api/v1/search/messages?q=smoke", "", token);
    ASSERT_EQ(search.code, 200) << search.body;
    EXPECT_EQ(TestApiApp::body_of(search).at("results").size(), 1U);

    // --- Delete message -----------------------------------------------------
    const auto deleted = app.request(
        crow::HTTPMethod::Delete, "/api/v1/messages/" + std::to_string(message_id), "", token);
    ASSERT_EQ(deleted.code, 200) << deleted.body;

    // --- Logout -------------------------------------------------------------
    const auto logout = app.request(crow::HTTPMethod::Post,
                                    "/api/v1/auth/logout",
                                    json{{"session_id", session_id}}.dump(),
                                    token);
    ASSERT_EQ(logout.code, 200) << logout.body;
    EXPECT_TRUE(TestApiApp::body_of(logout).at("revoked").get<bool>());
}

// The same flow must remain available to clients that never adopted the version
// prefix. Abbreviated to the state-changing steps: the parity matrix above
// already covers reachability of every route.
TEST(ApiSmoke, LegacyPrefixStillServesTheFlow) {
    TestApiApp app;
    ASSERT_EQ(
        app.request(
               crow::HTTPMethod::Post,
               "/api/auth/register",
               R"({"username":"ada","email":"ada@example.com","password":"correct-horse-battery"})")
            .code,
        201);
    const auto peer = app.request(
        crow::HTTPMethod::Post,
        "/api/auth/register",
        R"({"username":"bob","email":"bob@example.com","password":"correct-horse-battery"})");
    ASSERT_EQ(peer.code, 201);
    const auto peer_id = TestApiApp::body_of(peer).at("user").at("id").get<std::int64_t>();

    const auto login = app.request(crow::HTTPMethod::Post,
                                   "/api/auth/login",
                                   R"({"identifier":"ada","password":"correct-horse-battery"})");
    ASSERT_EQ(login.code, 200) << login.body;
    const auto token =
        TestApiApp::body_of(login).at("tokens").at("access_token").get<std::string>();

    const auto created =
        app.request(crow::HTTPMethod::Post,
                    "/api/conversations",
                    json{{"type", "direct"}, {"participant_ids", {peer_id}}}.dump(),
                    token);
    ASSERT_EQ(created.code, 201) << created.body;

    const auto sent = app.request(
        crow::HTTPMethod::Post,
        "/api/messages",
        json{{"conversation_id", TestApiApp::body_of(created).at("id").get<std::int64_t>()},
             {"content", "legacy prefix still works"}}
            .dump(),
        token);
    EXPECT_EQ(sent.code, 201) << sent.body;
}

}  // namespace
