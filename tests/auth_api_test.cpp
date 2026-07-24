#include <memory>
#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rtc/config/config.hpp"
#include "rtc/controllers/auth_controller.hpp"
#include "rtc/controllers/health_controller.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/user_service.hpp"
#include "support/fake_password_hasher.hpp"
#include "support/fake_user_repository.hpp"

namespace {

using nlohmann::json;

// Exercises the HTTP layer in-process via Crow's handle_full(), with fake
// persistence/hashing so no database or network socket is required. This is the
// Register/Login API coverage plus the health and protected-route checks.
class AuthApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        health_ = std::make_unique<rtc::controllers::HealthController>(config_);
        auth_ = std::make_unique<rtc::controllers::AuthController>(auth_service_, user_service_,
                                                                   guard_);
        health_->register_routes(app_);
        auth_->register_routes(app_);
        app_.validate();
    }

    crow::response do_request(crow::HTTPMethod method, const std::string& url,
                              const std::string& body = {},
                              const std::string& authorization = {}) {
        crow::request req;
        req.method = method;
        req.url = url;
        req.body = body;
        if (!body.empty()) {
            req.headers.emplace("Content-Type", "application/json");
        }
        if (!authorization.empty()) {
            req.headers.emplace("Authorization", authorization);
        }
        crow::response res;
        app_.handle_full(req, res);
        return res;
    }

    std::string register_body(const std::string& username, const std::string& email,
                              const std::string& password) {
        return json{{"username", username}, {"email", email}, {"password", password}}.dump();
    }

    rtc::config::Config config_{};
    rtc::testing::FakeUserRepository repo_;
    rtc::testing::FakePasswordHasher hasher_;
    rtc::security::JwtTokenService token_service_{
        rtc::security::JwtTokenService::Options{.secret = "api-test-secret",
                                                .issuer = "realtime-chat-test"}};
    rtc::services::UserService user_service_{repo_, hasher_};
    rtc::services::AuthService auth_service_{user_service_, token_service_};
    rtc::middlewares::AuthMiddleware guard_{token_service_};

    rtc::http::App app_;
    std::unique_ptr<rtc::controllers::HealthController> health_;
    std::unique_ptr<rtc::controllers::AuthController> auth_;
};

TEST_F(AuthApiTest, HealthReturnsOk) {
    const auto res = do_request(crow::HTTPMethod::Get, "/health");
    ASSERT_EQ(res.code, 200);
    const auto body = json::parse(res.body);
    EXPECT_EQ(body["status"], "ok");
    EXPECT_EQ(body["service"], "realtime-chat");
    EXPECT_EQ(body["environment"], "development");
}

TEST_F(AuthApiTest, RegisterReturns201WithTokens) {
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/register",
                                register_body("alice", "alice@example.com", "password123"));
    ASSERT_EQ(res.code, 201);
    const auto body = json::parse(res.body);
    EXPECT_EQ(body["user"]["username"], "alice");
    EXPECT_FALSE(body["user"].contains("password_hash"));
    EXPECT_TRUE(body["tokens"].contains("access_token"));
    EXPECT_EQ(body["tokens"]["token_type"], "Bearer");
}

TEST_F(AuthApiTest, RegisterDuplicateReturns409) {
    do_request(crow::HTTPMethod::Post, "/api/auth/register",
               register_body("alice", "alice@example.com", "password123"));
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/register",
                                register_body("alice", "other@example.com", "password123"));
    ASSERT_EQ(res.code, 409);
    EXPECT_EQ(json::parse(res.body)["error"]["code"], "conflict");
}

TEST_F(AuthApiTest, RegisterInvalidBodyReturns400) {
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/register", "{not json");
    ASSERT_EQ(res.code, 400);
    EXPECT_EQ(json::parse(res.body)["error"]["code"], "validation_error");
}

TEST_F(AuthApiTest, RegisterShortPasswordReturns400) {
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/register",
                                register_body("alice", "alice@example.com", "short"));
    ASSERT_EQ(res.code, 400);
}

TEST_F(AuthApiTest, LoginReturns200WithTokens) {
    do_request(crow::HTTPMethod::Post, "/api/auth/register",
               register_body("bob", "bob@example.com", "password123"));
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/login",
                                json{{"identifier", "bob"}, {"password", "password123"}}.dump());
    ASSERT_EQ(res.code, 200);
    EXPECT_TRUE(json::parse(res.body)["tokens"].contains("access_token"));
}

TEST_F(AuthApiTest, LoginWrongPasswordReturns401) {
    do_request(crow::HTTPMethod::Post, "/api/auth/register",
               register_body("bob", "bob@example.com", "password123"));
    const auto res = do_request(crow::HTTPMethod::Post, "/api/auth/login",
                                json{{"identifier", "bob"}, {"password", "nope"}}.dump());
    ASSERT_EQ(res.code, 401);
    EXPECT_EQ(json::parse(res.body)["error"]["code"], "authentication_error");
}

TEST_F(AuthApiTest, ProtectedRouteRejectsMissingToken) {
    const auto res = do_request(crow::HTTPMethod::Get, "/api/auth/me");
    EXPECT_EQ(res.code, 401);
}

TEST_F(AuthApiTest, ProtectedRouteAcceptsValidToken) {
    const auto reg = do_request(crow::HTTPMethod::Post, "/api/auth/register",
                                register_body("carol", "carol@example.com", "password123"));
    const auto token = json::parse(reg.body)["tokens"]["access_token"].get<std::string>();

    const auto res = do_request(crow::HTTPMethod::Get, "/api/auth/me", {}, "Bearer " + token);
    ASSERT_EQ(res.code, 200);
    EXPECT_EQ(json::parse(res.body)["username"], "carol");
}

}  // namespace
