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

    // Corrupt a character in the *middle* of the signature segment.
    //
    // Flipping the final character (the previous approach) is not reliable.
    // HS256's 32-byte signature base64url-encodes to 43 characters, and the last
    // one carries only 4 significant bits — its low 2 bits are padding. Two
    // characters differing solely in those padding bits, such as 'a' (26) and
    // 'b' (27), therefore decode to identical bytes, so "corrupting" the last
    // character was sometimes a no-op: the signature stayed valid, verify()
    // succeeded, and the test failed. That is a 1-in-64 flake, which is exactly
    // often enough to be seen occasionally in CI and never locally.
    //
    // Every bit of a non-final character is significant, so this always changes
    // the signature.
    const std::size_t signature_start = token.rfind('.') + 1;
    ASSERT_LT(signature_start, token.size()) << "token has no signature segment";
    const std::size_t target = signature_start + (token.size() - signature_start) / 2;
    token[target] = (token[target] == 'a') ? 'b' : 'a';

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
