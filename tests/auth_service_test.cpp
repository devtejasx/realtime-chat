#include "rtc/services/auth_service.hpp"

#include <gtest/gtest.h>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/security/token.hpp"
#include "rtc/services/user_service.hpp"
#include "support/fake_password_hasher.hpp"
#include "support/fake_user_repository.hpp"

namespace {

using rtc::dto::LoginRequest;
using rtc::dto::RegisterRequest;
using rtc::errors::AuthenticationException;
using rtc::errors::ValidationException;
using rtc::security::JwtTokenService;
using rtc::security::TokenType;
using rtc::services::AuthService;
using rtc::services::UserService;
using rtc::testing::FakePasswordHasher;
using rtc::testing::FakeUserRepository;

class AuthServiceTest : public ::testing::Test {
  protected:
    FakeUserRepository repo_;
    FakePasswordHasher hasher_;
    UserService user_service_{repo_, hasher_};
    JwtTokenService token_service_{JwtTokenService::Options{.secret = "auth-service-test-secret",
                                                            .issuer = "realtime-chat-test"}};
    AuthService auth_service_{user_service_, token_service_};

    RegisterRequest valid_register() {
        RegisterRequest r;
        r.username = "alice";
        r.email = "alice@example.com";
        r.password = "password123";
        return r;
    }
};

TEST_F(AuthServiceTest, RegisterReturnsUserAndValidTokens) {
    const auto response = auth_service_.register_user(valid_register());

    EXPECT_EQ(response.user.username, "alice");
    EXPECT_EQ(response.user.email, "alice@example.com");
    EXPECT_FALSE(response.tokens.access_token.empty());
    EXPECT_FALSE(response.tokens.refresh_token.empty());

    const auto claims = token_service_.verify(response.tokens.access_token, TokenType::kAccess);
    EXPECT_EQ(claims.user_id, response.user.id);
    EXPECT_EQ(claims.username, "alice");
}

TEST_F(AuthServiceTest, RegisterNormalizesInput) {
    RegisterRequest r;
    r.username = "  Alice  ";
    r.email = "  Alice@Example.com ";
    r.password = "password123";
    const auto response = auth_service_.register_user(r);
    EXPECT_EQ(response.user.username, "Alice");
    EXPECT_EQ(response.user.email, "alice@example.com");
}

TEST_F(AuthServiceTest, RegisterInvalidInputThrowsValidation) {
    RegisterRequest r;
    r.username = "a";  // too short
    r.email = "alice@example.com";
    r.password = "password123";
    EXPECT_THROW(auth_service_.register_user(r), ValidationException);
}

TEST_F(AuthServiceTest, LoginWithCorrectCredentialsSucceeds) {
    auth_service_.register_user(valid_register());

    LoginRequest login;
    login.identifier = "alice";
    login.password = "password123";
    const auto response = auth_service_.login(login);
    EXPECT_EQ(response.user.username, "alice");
    EXPECT_FALSE(response.tokens.access_token.empty());
}

TEST_F(AuthServiceTest, LoginWithWrongPasswordThrows) {
    auth_service_.register_user(valid_register());

    LoginRequest login;
    login.identifier = "alice";
    login.password = "not-the-password";
    EXPECT_THROW(auth_service_.login(login), AuthenticationException);
}

}  // namespace
