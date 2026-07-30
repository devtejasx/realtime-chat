// RBAC and the authorisation cache.
//
// Every authenticated request performs at least one authorisation lookup (the
// suspension check in AuthMiddleware), and admin routes perform two. The point of
// these benchmarks is to show the gap between a cached decision and an uncached one
// — that gap is the entire justification for AUTHZ_CACHE_TTL_SECONDS, and for
// resolving roles from the database rather than trusting the JWT.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/repositories/user_admin_repository.hpp"
#include "rtc/security/role.hpp"
#include "rtc/services/authorization_service.hpp"

namespace {

// A repository that counts lookups and can simulate query latency, so the cached
// and uncached paths are distinguishable without a real database.
class CountingUserAdminRepository final : public rtc::repositories::IUserAdminRepository {
  public:
    explicit CountingUserAdminRepository(rtc::security::Role role) : role_(role) {}

    [[nodiscard]] std::optional<rtc::security::Role> find_role(std::int64_t) override {
        ++lookups;
        return role_;
    }
    [[nodiscard]] std::optional<bool> is_banned(std::int64_t) override {
        ++lookups;
        return false;
    }
    [[nodiscard]] std::optional<rtc::repositories::AdminUserRecord> find(std::int64_t) override {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<rtc::repositories::AdminUserRecord> list(
        const rtc::repositories::AdminUserFilter&, const rtc::dto::Pagination&) override {
        return {};
    }
    [[nodiscard]] std::int64_t count(const rtc::repositories::AdminUserFilter&) override {
        return 0;
    }
    [[nodiscard]] rtc::security::Role set_role(std::int64_t, rtc::security::Role) override {
        return role_;
    }
    void set_banned(std::int64_t,
                    bool,
                    std::optional<std::string>,
                    std::optional<std::int64_t>) override {}
    [[nodiscard]] std::vector<std::pair<rtc::security::Role, std::int64_t>> counts_by_role()
        override {
        return {};
    }

    std::uint64_t lookups = 0;

  private:
    rtc::security::Role role_;
};

// The pure permission check: a constexpr bitmask test, so this should be a handful
// of nanoseconds and effectively free.
void BM_HasPermission(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::security::has_permission(
            rtc::security::Role::kAdmin, rtc::security::Permission::kViewAuditLogs));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HasPermission);

void BM_ParseRole(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::security::parse_role("moderator"));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ParseRole);

void BM_CanAssignRole(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(rtc::security::can_assign_role(rtc::security::Role::kSuperAdmin,
                                                                rtc::security::Role::kAdmin));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CanAssignRole);

// Cached role lookup — the common case, one cache read.
void BM_AuthorizationRoleCached(benchmark::State& state) {
    CountingUserAdminRepository repository(rtc::security::Role::kAdmin);
    rtc::cache::InMemoryCacheStore cache;
    rtc::services::AuthorizationService service(
        repository,
        cache,
        rtc::services::AuthorizationService::Options{std::chrono::seconds(3600)});

    // Warm the cache so the loop measures hits only.
    (void) service.role_of(42);

    for (auto _ : state) {
        benchmark::DoNotOptimize(service.role_of(42));
    }
    state.counters["repo_lookups"] = static_cast<double>(repository.lookups);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AuthorizationRoleCached);

// Uncached: every call invalidates first, forcing a repository round trip. With a
// real database this is a network hop plus a query — orders of magnitude above the
// cached path. That difference is why the cache exists, and why its TTL is a
// deliberate trade between staleness and load rather than an arbitrary number.
void BM_AuthorizationRoleUncached(benchmark::State& state) {
    CountingUserAdminRepository repository(rtc::security::Role::kAdmin);
    rtc::cache::InMemoryCacheStore cache;
    rtc::services::AuthorizationService service(
        repository,
        cache,
        rtc::services::AuthorizationService::Options{std::chrono::seconds(3600)});

    for (auto _ : state) {
        state.PauseTiming();
        service.invalidate(42);
        state.ResumeTiming();
        benchmark::DoNotOptimize(service.role_of(42));
    }
    state.counters["repo_lookups"] = static_cast<double>(repository.lookups);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AuthorizationRoleUncached);

// The suspension check AuthMiddleware performs on every authenticated request.
void BM_AuthorizationRequireActiveCached(benchmark::State& state) {
    CountingUserAdminRepository repository(rtc::security::Role::kUser);
    rtc::cache::InMemoryCacheStore cache;
    rtc::services::AuthorizationService service(
        repository,
        cache,
        rtc::services::AuthorizationService::Options{std::chrono::seconds(3600)});

    service.require_active(42);  // warm

    for (auto _ : state) {
        service.require_active(42);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AuthorizationRequireActiveCached);

// Full permission enforcement, as an admin route pays it.
void BM_AuthorizationRequirePermission(benchmark::State& state) {
    CountingUserAdminRepository repository(rtc::security::Role::kSuperAdmin);
    rtc::cache::InMemoryCacheStore cache;
    rtc::services::AuthorizationService service(
        repository,
        cache,
        rtc::services::AuthorizationService::Options{std::chrono::seconds(3600)});

    service.require_permission(42, rtc::security::Permission::kViewAuditLogs);  // warm

    for (auto _ : state) {
        service.require_permission(42, rtc::security::Permission::kViewAuditLogs);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AuthorizationRequirePermission);

}  // namespace
