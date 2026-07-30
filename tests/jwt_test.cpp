#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "rtc/security/token.hpp"

namespace {

using rtc::errors::AuthenticationException;
using rtc::security::JwtTokenService;
using rtc::security::TokenType;

JwtTokenService make_service(std::int64_t access_ttl = 900) {
    return JwtTokenService(JwtTokenService::Options{
        .secret = "unit-test-secret-value-please-ignore",
        .issuer = "realtime-chat-test",
        .access_ttl_seconds = access_ttl,
        .refresh_ttl_seconds = 1'209'600,
    });
}

TEST(JwtTokenServiceTest, IssueAndVerifyRoundTrip) {
    const auto service = make_service();
    const std::string token = service.issue(42, "alice", TokenType::kAccess);

    const auto claims = service.verify(token, TokenType::kAccess);
    EXPECT_EQ(claims.user_id, 42);
    EXPECT_EQ(claims.username, "alice");
    EXPECT_EQ(claims.type, TokenType::kAccess);
}

TEST(JwtTokenServiceTest, IssuePairProducesBothTokens) {
    const auto service = make_service();
    const auto pair = service.issue_pair(7, "bob");
    EXPECT_FALSE(pair.access_token.empty());
    EXPECT_FALSE(pair.refresh_token.empty());
    EXPECT_EQ(pair.access_expires_in_seconds, 900);

    EXPECT_NO_THROW(service.verify(pair.access_token, TokenType::kAccess));
    EXPECT_NO_THROW(service.verify(pair.refresh_token, TokenType::kRefresh));
}

TEST(JwtTokenServiceTest, RejectsWrongTokenType) {
    const auto service = make_service();
    const std::string access = service.issue(1, "carol", TokenType::kAccess);
    // An access token must not pass as a refresh token.
    EXPECT_THROW(service.verify(access, TokenType::kRefresh), AuthenticationException);
}

TEST(JwtTokenServiceTest, RejectsTamperedToken) {
    const auto service = make_service();
    std::string token = service.issue(1, "dave", TokenType::kAccess);
    token.back() = (token.back() == 'a') ? 'b' : 'a';  // corrupt the signature
    EXPECT_THROW(service.verify(token, TokenType::kAccess), AuthenticationException);
}

TEST(JwtTokenServiceTest, RejectsTokenSignedWithDifferentSecret) {
    const auto issuer = make_service();
    const std::string token = issuer.issue(1, "erin", TokenType::kAccess);

    JwtTokenService other(JwtTokenService::Options{.secret = "a-completely-different-secret",
                                                   .issuer = "realtime-chat-test"});
    EXPECT_THROW(other.verify(token, TokenType::kAccess), AuthenticationException);
}

TEST(JwtTokenServiceTest, RejectsExpiredToken) {
    const auto service = make_service(/*access_ttl=*/-1);  // exp in the past
    const std::string token = service.issue(1, "frank", TokenType::kAccess);
    EXPECT_THROW(service.verify(token, TokenType::kAccess), AuthenticationException);
}

TEST(JwtTokenServiceTest, RejectsGarbageInput) {
    const auto service = make_service();
    EXPECT_THROW(service.verify("not.a.jwt", TokenType::kAccess), AuthenticationException);
}

}  // namespace
