#include "rtc/database/migration_runner.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <pqxx/transaction>

#include "rtc/errors/exceptions.hpp"
#include "rtc/logging/logger.hpp"

namespace rtc::database {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw rtc::errors::DatabaseException("Cannot open migration file",
                                             path.filename().string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}  // namespace

MigrationRunner::MigrationRunner(ConnectionPool& pool, fs::path migrations_dir)
    : pool_(pool), migrations_dir_(std::move(migrations_dir)) {}

void MigrationRunner::ensure_bookkeeping_table() {
    PooledConnection lease = pool_.acquire();
    pqxx::work txn(lease.get());
    txn.exec(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version    TEXT PRIMARY KEY,"
        "  applied_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")");
    txn.commit();
}

bool MigrationRunner::is_applied(const std::string& version) {
    PooledConnection lease = pool_.acquire();
    pqxx::work txn(lease.get());
    const auto row = txn.exec_params1(
        "SELECT COUNT(*) FROM schema_migrations WHERE version = $1", version);
    txn.commit();
    return row[0].as<int>() > 0;
}

void MigrationRunner::apply(const std::string& version, const std::string& sql) {
    PooledConnection lease = pool_.acquire();
    pqxx::work txn(lease.get());
    // Execute the migration body and record it atomically.
    txn.exec(sql);
    txn.exec_params("INSERT INTO schema_migrations (version) VALUES ($1)", version);
    txn.commit();
}

int MigrationRunner::run() {
    if (!fs::exists(migrations_dir_) || !fs::is_directory(migrations_dir_)) {
        throw rtc::errors::DatabaseException("Migrations directory not found",
                                             migrations_dir_.string());
    }

    ensure_bookkeeping_table();

    // Collect and deterministically order migration files by filename.
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(migrations_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sql") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end(),
              [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });

    int applied_count = 0;
    for (const auto& file : files) {
        const std::string version = file.stem().string();
        if (is_applied(version)) {
            RTC_LOG_DEBUG("Migration already applied: {}", version);
            continue;
        }
        RTC_LOG_INFO("Applying migration: {}", version);
        try {
            apply(version, read_file(file));
        } catch (const rtc::errors::AppException&) {
            throw;
        } catch (const std::exception& ex) {
            throw rtc::errors::DatabaseException("Migration failed: " + version, ex.what());
        }
        ++applied_count;
    }

    RTC_LOG_INFO("Migrations complete; {} applied, {} total", applied_count, files.size());
    return applied_count;
}

}  // namespace rtc::database
