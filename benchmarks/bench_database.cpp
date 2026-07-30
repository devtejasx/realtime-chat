// PostgreSQL benchmarks.
//
// These require a live database and are therefore *skipped by default*. A benchmark
// that silently fails, or worse reports the cost of throwing a connection
// exception, is actively misleading — so each one checks for a usable connection
// first and marks itself skipped when there is none.
//
// Run them against a real database with:
//
//     export BENCH_DB=1 DB_HOST=localhost DB_NAME=realtime_chat
//     export DB_USER=chat DB_PASSWORD=chat_password
//     ./rtc_benchmarks --benchmark_filter=Db
//
// docker-compose.dev.yml provides a suitable instance.
//
// Interpretation matters here. These numbers are dominated by round-trip latency to
// the database, not by CPU, so they are only comparable *between runs against the
// same database*. Use them to detect a query regression, not to compare machines.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <pqxx/transaction>
#include <string>

#include "rtc/config/config.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/utils/env.hpp"

namespace {

// True only when explicitly opted in, so a normal benchmark run never depends on
// external state.
[[nodiscard]] bool database_benchmarks_enabled() {
    const auto flag = rtc::utils::get_env("BENCH_DB");
    return flag.has_value() && (*flag == "1" || *flag == "true");
}

// A pool shared across benchmarks, created once. Constructing a pool per benchmark
// would measure connection establishment rather than query cost.
rtc::database::ConnectionPool* shared_pool() {
    static std::unique_ptr<rtc::database::ConnectionPool> pool = [] {
        std::unique_ptr<rtc::database::ConnectionPool> created;
        try {
            const auto config = rtc::config::Config::load_from_env();
            created = std::make_unique<rtc::database::ConnectionPool>(
                config.database_connection_string(), 4);
            // Prove it actually works before any benchmark relies on it.
            auto lease = created->acquire();
            pqxx::work txn(lease.get());
            txn.exec("SELECT 1");
            txn.commit();
        } catch (const std::exception&) {
            created.reset();
        }
        return created;
    }();
    return pool.get();
}

// Guard shared by every benchmark below.
[[nodiscard]] bool skip_unless_available(benchmark::State& state) {
    if (!database_benchmarks_enabled()) {
        state.SkipWithError("set BENCH_DB=1 to run database benchmarks");
        return true;
    }
    if (shared_pool() == nullptr) {
        state.SkipWithError("no usable database connection (check DB_* environment variables)");
        return true;
    }
    return false;
}

// Round-trip floor: the cheapest possible statement. Everything else should be read
// relative to this — a query taking 3x this is doing real work; one taking 50x is
// worth investigating.
void BM_DbSelectOne(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();

    for (auto _ : state) {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        benchmark::DoNotOptimize(txn.exec("SELECT 1"));
        txn.commit();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DbSelectOne);

// Pool acquisition alone, with no query. Isolates lock contention in the pool from
// database latency; should be negligible, and if it is not, the pool is too small
// for the thread count.
void BM_DbPoolAcquire(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();

    for (auto _ : state) {
        auto lease = pool->acquire();
        benchmark::DoNotOptimize(&lease);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DbPoolAcquire)->Threads(1)->Threads(2)->Threads(4);

// A parameterised statement, which is what every repository actually issues.
// Comparing against BM_DbSelectOne shows the cost of parameter binding and planning.
void BM_DbParameterisedQuery(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();

    for (auto _ : state) {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        benchmark::DoNotOptimize(
            txn.exec_params("SELECT id, username FROM users WHERE id = $1", 1));
        txn.commit();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DbParameterisedQuery);

// Full-text search over messages, using the stored tsvector added by migration
// 0013. The interesting comparison is against the pre-migration behaviour, which
// computed to_tsvector per row at query time: with the GIN index the cost should be
// roughly independent of table size.
void BM_DbFullTextSearch(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();

    for (auto _ : state) {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        benchmark::DoNotOptimize(txn.exec_params(
            "SELECT id, ts_rank_cd(search_vector, websearch_to_tsquery('english', $1)) AS rank "
            "FROM messages "
            "WHERE deleted_at IS NULL AND search_vector @@ websearch_to_tsquery('english', $1) "
            "ORDER BY rank DESC LIMIT 20",
            "hello"));
        txn.commit();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DbFullTextSearch);

// The conversation-history query — the single most frequent read in the system.
// Backed by ix_messages_conversation_id (conversation_id, id DESC), so it should be
// an index scan with no sort.
void BM_DbMessageHistory(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();

    for (auto _ : state) {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        benchmark::DoNotOptimize(
            txn.exec_params("SELECT id, sender_id, content, created_at FROM messages "
                            "WHERE conversation_id = $1 ORDER BY id DESC LIMIT 50",
                            1));
        txn.commit();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_DbMessageHistory);

// The audit append, which runs on the worker pool rather than the request path. Its
// ON CONFLICT DO NOTHING makes it idempotent; this measures whether that costs
// anything meaningful.
void BM_DbAuditAppend(benchmark::State& state) {
    if (skip_unless_available(state)) {
        return;
    }
    auto* pool = shared_pool();
    std::int64_t counter = 0;

    for (auto _ : state) {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        txn.exec_params(
            "INSERT INTO audit_logs (event_id, event_type, metadata) "
            "VALUES ($1, $2, $3::jsonb) ON CONFLICT (event_id) DO NOTHING",
            "bench-" + std::to_string(counter++),
            "bench.event",
            "{}");
        txn.commit();
    }
    state.SetItemsProcessed(state.iterations());

    // Clean up so repeated runs do not accumulate rows.
    try {
        auto lease = pool->acquire();
        pqxx::work txn(lease.get());
        txn.exec("DELETE FROM audit_logs WHERE event_type = 'bench.event'");
        txn.commit();
    } catch (const std::exception&) {
        // Best effort; a leftover row is harmless.
    }
}
BENCHMARK(BM_DbAuditAppend);

}  // namespace
