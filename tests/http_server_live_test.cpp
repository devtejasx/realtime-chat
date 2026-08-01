#include <gtest/gtest.h>

#include <asio.hpp>
#include <cstdint>
#include <future>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/http/api_version.hpp"
#include "support/test_api_app.hpp"

// End-to-end coverage over a real TCP socket.
//
// api_http_integration_test.cpp drives requests through crow::App::handle_full(),
// which routes and dispatches but never runs the global middleware chain — Crow
// invokes that from crow::http_connection, not from the app object. Everything
// ApiVersionMiddleware is responsible for is therefore invisible to that suite:
// the X-API-Version header, and the rejection of a version this build does not
// serve.
//
// This suite starts the server for real and speaks HTTP/1.1 to it, so the full
// connection path runs — the same path that produced the original 404, where
// handle_url() resolved the route before any middleware could touch the URL.

namespace {

using nlohmann::json;

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;  // lower-cased names

    [[nodiscard]] std::string header(const std::string& name) const {
        const auto it = headers.find(name);
        return it == headers.end() ? std::string{} : it->second;
    }
};

// Asks the OS for a free port, then releases it. A racing bind between here and
// the server's own is possible in principle; in practice the window is a few
// microseconds and the alternative (a hardcoded port) collides far more often on
// a busy CI machine.
std::uint16_t reserve_port() {
    asio::io_context io;
    asio::ip::tcp::acceptor probe(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
    return probe.local_endpoint().port();
}

// Minimal HTTP/1.1 client. Sends `Connection: close` so the response body is
// delimited by EOF and no chunked/keep-alive handling is needed.
HttpResponse http_request(std::uint16_t port,
                          const std::string& method,
                          const std::string& target,
                          const std::string& body = {},
                          const std::string& bearer = {}) {
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    asio::ip::tcp::resolver resolver(io);
    asio::connect(socket, resolver.resolve("127.0.0.1", std::to_string(port)));

    std::string request = method + " " + target + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Connection: close\r\n";
    if (!bearer.empty()) {
        request += "Authorization: Bearer " + bearer + "\r\n";
    }
    if (!body.empty()) {
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "\r\n" + body;
    asio::write(socket, asio::buffer(request));

    std::string raw;
    asio::error_code ec;
    char buffer[4096];
    while (true) {
        const std::size_t n = socket.read_some(asio::buffer(buffer), ec);
        if (ec) {
            break;
        }
        raw.append(buffer, n);
    }

    HttpResponse response;
    const auto header_end = raw.find("\r\n\r\n");
    const std::string head = raw.substr(0, header_end);
    if (header_end != std::string::npos) {
        response.body = raw.substr(header_end + 4);
    }

    std::size_t line_start = 0;
    bool first = true;
    while (line_start < head.size()) {
        auto line_end = head.find("\r\n", line_start);
        if (line_end == std::string::npos) {
            line_end = head.size();
        }
        const std::string line = head.substr(line_start, line_end - line_start);
        if (first) {
            const auto space = line.find(' ');
            if (space != std::string::npos) {
                response.status = std::stoi(line.substr(space + 1, 3));
            }
            first = false;
        } else {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                for (auto& c : name) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                auto value = line.substr(colon + 1);
                const auto first_char = value.find_first_not_of(" \t");
                response.headers[name] =
                    first_char == std::string::npos ? std::string{} : value.substr(first_char);
            }
        }
        line_start = line_end + 2;
    }
    return response;
}

// Boots the wired-up application on a real port for the lifetime of the fixture.
class LiveServerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        port_ = reserve_port();
        app_.app().loglevel(crow::LogLevel::Critical);
        server_ = app_.app().port(port_).concurrency(1).run_async();
        app_.app().wait_for_server_start();
    }

    void TearDown() override {
        app_.app().stop();
        if (server_.valid()) {
            server_.wait();
        }
    }

    HttpResponse get(const std::string& target, const std::string& bearer = {}) {
        return http_request(port_, "GET", target, {}, bearer);
    }
    HttpResponse post(const std::string& target,
                      const std::string& body,
                      const std::string& bearer = {}) {
        return http_request(port_, "POST", target, body, bearer);
    }

    rtc::testing::TestApiApp app_;
    std::uint16_t port_ = 0;
    std::future<void> server_;
};

TEST_F(LiveServerTest, ServesBothPrefixesOverARealSocket) {
    // The original defect reproduced exactly: over a live connection, Crow
    // resolved the route in handle_url() before the version middleware ran, so
    // this request 404'd.
    const auto versioned =
        post("/api/v1/auth/register",
             R"({"username":"ada","email":"ada@example.com","password":"correct-horse-battery"})");
    EXPECT_EQ(versioned.status, 201) << versioned.body;

    const auto legacy =
        post("/api/auth/register",
             R"({"username":"bob","email":"bob@example.com","password":"correct-horse-battery"})");
    EXPECT_EQ(legacy.status, 201) << legacy.body;
}

TEST_F(LiveServerTest, StampsTheServedApiVersionOnBothPrefixes) {
    const auto expected = rtc::http::api_version_label(rtc::http::kCurrentApiVersion);
    for (const char* path : {"/api/auth/me", "/api/v1/auth/me"}) {
        const auto res = get(path);
        EXPECT_EQ(res.header("x-api-version"), expected) << "for " << path;
    }
}

TEST_F(LiveServerTest, RejectsAnUnsupportedVersionWithAMachineReadableError) {
    const auto res =
        post("/api/v9/auth/login", R"({"identifier":"ada","password":"correct-horse-battery"})");
    ASSERT_EQ(res.status, 404) << res.body;
    const auto body = json::parse(res.body, nullptr, false);
    ASSERT_FALSE(body.is_discarded()) << res.body;
    EXPECT_EQ(body.at("error").at("code"), "unsupported_api_version");
    EXPECT_EQ(body.at("error").at("details"),
              "supported=" + rtc::http::supported_api_versions_label());

    // Must advertise what the server speaks, not the version it just refused.
    EXPECT_EQ(res.header("x-api-version"),
              rtc::http::api_version_label(rtc::http::kCurrentApiVersion));
}

TEST_F(LiveServerTest, UnknownPathUnderASupportedVersionReturnsTheErrorEnvelope) {
    const auto res = get("/api/v1/definitely-not-a-route");
    ASSERT_EQ(res.status, 404) << res.body;
    const auto body = json::parse(res.body, nullptr, false);
    ASSERT_FALSE(body.is_discarded()) << res.body;
    EXPECT_EQ(body.at("error").at("code"), "not_found");
}

TEST_F(LiveServerTest, SecurityHeadersSurviveTheVersionedPrefix) {
    // The version catch-all short-circuits inside a middleware. It must still
    // unwind the outer middlewares so the response keeps its security headers.
    const auto res = get("/api/v9/anything");
    EXPECT_EQ(res.header("x-content-type-options"), "nosniff");
    EXPECT_EQ(res.header("x-frame-options"), "DENY");
}

TEST_F(LiveServerTest, QueryStringsSurviveOnBothPrefixes) {
    const auto token = app_.register_user("carol", "carol@example.com");
    ASSERT_FALSE(token.empty());
    for (const char* path : {"/api/search/messages?q=hello", "/api/v1/search/messages?q=hello"}) {
        const auto res = get(path, token);
        EXPECT_EQ(res.status, 200) << path << " -> " << res.body;
    }
}

TEST_F(LiveServerTest, FullFlowOverRealHttpOnTheVersionedPrefix) {
    const auto registered =
        post("/api/v1/auth/register",
             R"({"username":"ada","email":"ada@example.com","password":"correct-horse-battery"})");
    ASSERT_EQ(registered.status, 201) << registered.body;

    const auto peer =
        post("/api/v1/auth/register",
             R"({"username":"bob","email":"bob@example.com","password":"correct-horse-battery"})");
    ASSERT_EQ(peer.status, 201) << peer.body;
    const auto peer_id = json::parse(peer.body).at("user").at("id").get<std::int64_t>();

    const auto login =
        post("/api/v1/auth/login", R"({"identifier":"ada","password":"correct-horse-battery"})");
    ASSERT_EQ(login.status, 200) << login.body;
    const auto login_body = json::parse(login.body);
    const auto token = login_body.at("tokens").at("access_token").get<std::string>();

    const auto conversation = post("/api/v1/conversations",
                                   json{{"type", "direct"}, {"participant_ids", {peer_id}}}.dump(),
                                   token);
    ASSERT_EQ(conversation.status, 201) << conversation.body;
    const auto conversation_id = json::parse(conversation.body).at("id").get<std::int64_t>();

    const auto sent =
        post("/api/v1/messages",
             json{{"conversation_id", conversation_id}, {"content", "over a real socket"}}.dump(),
             token);
    ASSERT_EQ(sent.status, 201) << sent.body;

    const auto listed =
        get("/api/v1/messages?conversation_id=" + std::to_string(conversation_id), token);
    ASSERT_EQ(listed.status, 200) << listed.body;
    EXPECT_EQ(json::parse(listed.body).at("messages").size(), 1U);
}

}  // namespace
