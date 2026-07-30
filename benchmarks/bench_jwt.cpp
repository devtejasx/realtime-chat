// JWT issuance and verification.
//
// Verification runs on *every authenticated request*, so its cost is a fixed floor
// under the entire API's latency. Issuance runs once per login, so it matters far
// less — the two are separated here rather than measured together, because a
// combined figure would hide which one is on the hot path.
#include <string>

#include <benchmark/benchmark.h>

#include "rtc/security/jwt_token_service.hpp"

namespace {

[[nodiscard]] rtc::security::JwtTokenService make_service() {
    return rtc::security::JwtTokenService(rtc::security::JwtTokenService::Options{
        .secret = "benchmark-secret-at-least-32-bytes-long-for-hs256",
        .issuer = "realtime-chat",
        .access_ttl_seconds = 900,
        .refresh_ttl_seconds = 1'209'600,
    });
}

// Issue a single access token. Once per login.
void BM_JwtIssueAccessToken(benchmark::State& state) {
    const auto service = make_service();
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            service.issue(42, "benchmark-user", rtc::security::TokenType::kAccess));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_JwtIssueAccessToken);

// Issue an access + refresh pair. Two signatures, so roughly twice the above.
void BM_JwtIssuePair(benchmark::State& state) {
    const auto service = make_service();
    for (auto _ : state) {
        benchmark::DoNotOptimize(service.issue_pair(42, "benchmark-user"));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_JwtIssuePair);

// THE hot path: verification on every authenticated request. The token is created
// outside the loop so only verification is measured.
void BM_JwtVerifyAccessToken(benchmark::State& state) {
    const auto service = make_service();
    const std::string token =
        service.issue(42, "benchmark-user", rtc::security::TokenType::kAccess);

    for (auto _ : state) {
        benchmark::DoNotOptimize(service.verify(token, rtc::security::TokenType::kAccess));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_JwtVerifyAccessToken);

// Rejection path. Worth measuring separately: a rejection throws, and exception
// unwinding is dramatically more expensive than the success path. That makes
// invalid-token traffic a cheap way to load the server, which is precisely why
// /api/v1/auth/login is rate limited.
void BM_JwtVerifyInvalidSignature(benchmark::State& state) {
    const auto service = make_service();
    std::string tampered =
        service.issue(42, "benchmark-user", rtc::security::TokenType::kAccess);
    tampered.back() = tampered.back() == 'a' ? 'b' : 'a';

    for (auto _ : state) {
        try {
            benchmark::DoNotOptimize(
                service.verify(tampered, rtc::security::TokenType::kAccess));
        } catch (const std::exception&) {
            // Expected; the throw is the thing being measured.
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_JwtVerifyInvalidSignature);

// Verifying a refresh token as an access token must fail on the type claim. Should
// be cheaper than a signature failure, because the claim check happens after a
// *successful* signature verification but avoids no work — measured to confirm the
// type check is not accidentally the expensive part.
void BM_JwtVerifyWrongTokenType(benchmark::State& state) {
    const auto service = make_service();
    const std::string refresh =
        service.issue(42, "benchmark-user", rtc::security::TokenType::kRefresh);

    for (auto _ : state) {
        try {
            benchmark::DoNotOptimize(
                service.verify(refresh, rtc::security::TokenType::kAccess));
        } catch (const std::exception&) {
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_JwtVerifyWrongTokenType);

}  // namespace
