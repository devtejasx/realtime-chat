#pragma once

#include <exception>
#include <pqxx/transaction>
#include <type_traits>
#include <utility>

#include "rtc/database/connection_pool.hpp"
#include "rtc/errors/exceptions.hpp"

namespace rtc::database {

// Base class for all repositories.
//
// Provides a single transactional execution primitive so concrete repositories
// contain only SQL, never connection lifecycle or boilerplate error handling.
// `with_transaction` acquires a pooled connection, opens a transaction, runs
// the callable, commits on success, and rolls back on any exception.
//
// Error policy: application exceptions (rtc::errors::AppException and
// subclasses, e.g. a ConflictException raised by the callable on a unique
// violation) propagate unchanged; every other exception is wrapped into a
// DatabaseException so upper layers see a consistent error taxonomy.
class BaseRepository {
  public:
    virtual ~BaseRepository() = default;

  protected:
    explicit BaseRepository(ConnectionPool& pool) noexcept : pool_(pool) {}

    template <typename Fn>
    auto with_transaction(Fn&& fn) -> std::invoke_result_t<Fn, pqxx::work&> {
        using Result = std::invoke_result_t<Fn, pqxx::work&>;

        // The single choke point for every repository operation, and therefore
        // where the database circuit breaker belongs. Guarding here rather than
        // at each of ~11 repositories means a new repository is protected the
        // moment it is written.
        auto* breaker = pool_.circuit_breaker();
        if (breaker != nullptr && !breaker->allow()) {
            // Fail immediately instead of queueing behind a dependency that is
            // not answering. Without this, each caller waits out the pool
            // acquire timeout holding a worker thread, and the pool fills with
            // threads waiting on a database that is down — at which point
            // endpoints that never touch it start timing out too.
            throw rtc::errors::DatabaseException("Database is unavailable", "circuit=open");
        }

        try {
            PooledConnection lease = pool_.acquire();
            pqxx::work txn(lease.get());
            if constexpr (std::is_void_v<Result>) {
                std::forward<Fn>(fn)(txn);
                txn.commit();
                if (breaker != nullptr) {
                    breaker->on_success();
                }
            } else {
                Result result = std::forward<Fn>(fn)(txn);
                txn.commit();
                if (breaker != nullptr) {
                    breaker->on_success();
                }
                return result;
            }
        } catch (const rtc::errors::AppException&) {
            // A domain error counts as a *success* for the breaker, which is the
            // subtle part: a duplicate username or a missing row means the
            // database answered correctly and promptly. Counting those as
            // dependency failures would let ordinary user input — a burst of
            // conflicting registrations — trip the breaker and take the database
            // offline for everyone.
            if (breaker != nullptr) {
                breaker->on_success();
            }
            throw;  // domain errors pass through untouched
        } catch (const std::exception& ex) {
            // Everything else is the dependency failing: connection refused,
            // timeout, protocol error.
            if (breaker != nullptr) {
                breaker->on_failure();
            }
            throw rtc::errors::DatabaseException("Database operation failed", ex.what());
        }
    }

    [[nodiscard]] ConnectionPool& pool() noexcept { return pool_; }

  private:
    ConnectionPool& pool_;
};

}  // namespace rtc::database
