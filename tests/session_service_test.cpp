#include "rtc/services/session_service.hpp"

#include <gtest/gtest.h>

#include "rtc/errors/exceptions.hpp"
#include "rtc/security/jwt_token_service.hpp"
#include "support/fake_session_repository.hpp"

namespace {

using rtc::errors::AuthenticationException;
using rtc::security::JwtTokenService;

class SessionServiceTest : public ::testing::Test {
protected:
    JwtTokenService tokens_{JwtTokenService::Options{.secret = "session-test-secret",
                                                     .issuer = "realtime-chat-test"}};
    rtc::testing::FakeSessionRepository repo_;
    rtc::services::SessionService service_{repo_, tokens_, 1209600};
};

TEST_F(SessionServiceTest, RecordCreatesSession) {
    const auto pair = tokens_.issue_pair(1, "alice");
    const std::string session_id = service_.record(1, pair.refresh_token, "agent", "1.2.3.4");
    EXPECT_FALSE(session_id.empty());
    EXPECT_EQ(repo_.count(), 1U);
    EXPECT_EQ(service_.list(1).size(), 1U);
}

TEST_F(SessionServiceTest, RotateIssuesNewTokens) {
    const auto pair = tokens_.issue_pair(1, "alice");
    const std::string session_id = service_.record(1, pair.refresh_token, {}, {});

    const auto rotated = service_.rotate(pair.refresh_token, session_id);
    EXPECT_FALSE(rotated.access_token.empty());
    EXPECT_NE(rotated.refresh_token, pair.refresh_token);
}

TEST_F(SessionServiceTest, OldRefreshTokenRejectedAfterRotation) {
    const auto pair = tokens_.issue_pair(1, "alice");
    const std::string session_id = service_.record(1, pair.refresh_token, {}, {});
    service_.rotate(pair.refresh_token, session_id);
    // Replay protection: the original token no longer matches the stored hash.
    EXPECT_THROW(service_.rotate(pair.refresh_token, session_id), AuthenticationException);
}

TEST_F(SessionServiceTest, RotateWithUnknownSessionFails) {
    const auto pair = tokens_.issue_pair(1, "alice");
    EXPECT_THROW(service_.rotate(pair.refresh_token, "nope"), AuthenticationException);
}

TEST_F(SessionServiceTest, RevokeAndRevokeAll) {
    const auto p1 = tokens_.issue_pair(1, "alice");
    const auto p2 = tokens_.issue_pair(1, "alice");
    const std::string s1 = service_.record(1, p1.refresh_token, {}, {});
    service_.record(1, p2.refresh_token, {}, {});
    EXPECT_EQ(service_.list(1).size(), 2U);

    EXPECT_TRUE(service_.revoke(1, s1));
    EXPECT_EQ(service_.list(1).size(), 1U);

    EXPECT_GE(service_.revoke_all(1), 1);
    EXPECT_EQ(service_.list(1).size(), 0U);
}

}  // namespace
