#include "rtc/database/connection_pool.hpp"

#include <utility>

#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"

namespace rtc::database {

// ---------------------------------------------------------------------------
// PooledConnection
// ---------------------------------------------------------------------------
PooledConnection::~PooledConnection() { return_to_pool(); }

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
    if (this != &other) {
        return_to_pool();
        pool_ = other.pool_;
        conn_ = std::move(other.conn_);
        other.pool_ = nullptr;
    }
    return *this;
}

void PooledConnection::return_to_pool() noexcept {
    if (pool_ != nullptr && conn_ != nullptr) {
        pool_->release(std::move(conn_));
    }
    pool_ = nullptr;
    conn_.reset();
}

// ---------------------------------------------------------------------------
// ConnectionPool
// ---------------------------------------------------------------------------
ConnectionPool::ConnectionPool(std::string connection_string, std::size_t size)
    : connection_string_(std::move(connection_string)), size_(size == 0 ? 1 : size) {
    for (std::size_t i = 0; i < size_; ++i) {
        idle_.push(make_connection());
    }
    RTC_LOG_INFO("Database connection pool initialised with {} connection(s)", size_);
}

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
    while (!idle_.empty()) {
        idle_.pop();
    }
}

std::unique_ptr<pqxx::connection> ConnectionPool::make_connection() const {
    try {
        auto conn = std::make_unique<pqxx::connection>(connection_string_);
        if (!conn->is_open()) {
            throw rtc::errors::DatabaseException("Database connection is not open");
        }
        return conn;
    } catch (const rtc::errors::DatabaseException&) {
        throw;
    } catch (const std::exception& ex) {
        throw rtc::errors::DatabaseException("Failed to open database connection", ex.what());
    }
}

PooledConnection ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    available_.wait(lock, [this] { return shutting_down_ || !idle_.empty(); });
    if (shutting_down_) {
        throw rtc::errors::DatabaseException("Connection pool is shutting down");
    }

    auto conn = std::move(idle_.front());
    idle_.pop();
    lock.unlock();

    // Replace a connection that has died since it was last returned so callers
    // never receive a broken handle.
    if (conn == nullptr || !conn->is_open()) {
        RTC_LOG_WARN("Replacing a dead pooled database connection");
        conn = make_connection();
    }
    return PooledConnection(this, std::move(conn));
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutting_down_) {
        return;  // let the connection be destroyed
    }
    idle_.push(std::move(conn));
    available_.notify_one();
}

std::size_t ConnectionPool::idle_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return idle_.size();
}

}  // namespace rtc::database
