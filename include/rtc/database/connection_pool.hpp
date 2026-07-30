#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <pqxx/connection>
#include <queue>
#include <string>

namespace rtc::database {

class ConnectionPool;

// RAII lease over a pooled libpqxx connection.
//
// Acquired from ConnectionPool::acquire(), it returns the underlying
// connection to the pool on destruction. Move-only: ownership of the lease is
// unique. Access the connection through operator-> / get().
class PooledConnection {
  public:
    PooledConnection(ConnectionPool* pool, std::unique_ptr<pqxx::connection> conn) noexcept
        : pool_(pool), conn_(std::move(conn)) {}

    ~PooledConnection();

    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    PooledConnection(PooledConnection&& other) noexcept
        : pool_(other.pool_), conn_(std::move(other.conn_)) {
        other.pool_ = nullptr;
    }
    PooledConnection& operator=(PooledConnection&& other) noexcept;

    [[nodiscard]] pqxx::connection& get() const noexcept { return *conn_; }
    [[nodiscard]] pqxx::connection* operator->() const noexcept { return conn_.get(); }
    [[nodiscard]] pqxx::connection& operator*() const noexcept { return *conn_; }
    [[nodiscard]] bool valid() const noexcept { return conn_ != nullptr; }

  private:
    void return_to_pool() noexcept;

    ConnectionPool* pool_;
    std::unique_ptr<pqxx::connection> conn_;
};

// A fixed-size, thread-safe pool of PostgreSQL connections.
//
// Connections are created eagerly at construction. acquire() blocks until a
// connection is available (or the timeout elapses). Broken connections are
// transparently replaced when leased, so callers always receive an open
// connection. The pool is non-copyable and outlives every lease it hands out.
class ConnectionPool {
  public:
    // Builds `size` open connections from `connection_string`. Throws
    // rtc::errors::DatabaseException if the initial connections cannot be made.
    ConnectionPool(std::string connection_string, std::size_t size);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Leases a connection, blocking until one is free. The returned lease
    // guarantees an open connection.
    [[nodiscard]] PooledConnection acquire();

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t idle_count();

  private:
    friend class PooledConnection;

    [[nodiscard]] std::unique_ptr<pqxx::connection> make_connection() const;
    void release(std::unique_ptr<pqxx::connection> conn) noexcept;

    std::string connection_string_;
    std::size_t size_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::queue<std::unique_ptr<pqxx::connection>> idle_;
    bool shutting_down_ = false;
};

}  // namespace rtc::database
