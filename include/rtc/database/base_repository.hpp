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
        try {
            PooledConnection lease = pool_.acquire();
            pqxx::work txn(lease.get());
            if constexpr (std::is_void_v<Result>) {
                std::forward<Fn>(fn)(txn);
                txn.commit();
            } else {
                Result result = std::forward<Fn>(fn)(txn);
                txn.commit();
                return result;
            }
        } catch (const rtc::errors::AppException&) {
            throw;  // domain errors pass through untouched
        } catch (const std::exception& ex) {
            throw rtc::errors::DatabaseException("Database operation failed", ex.what());
        }
    }

    [[nodiscard]] ConnectionPool& pool() noexcept { return pool_; }

  private:
    ConnectionPool& pool_;
};

}  // namespace rtc::database
