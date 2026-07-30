#pragma once

#include <filesystem>
#include <string>

#include "rtc/database/connection_pool.hpp"

namespace rtc::database {

// Applies SQL schema migrations from a directory, idempotently and in order.
//
// Migration files are plain `.sql` scripts named with a sortable numeric
// prefix, e.g. `0001_create_users.sql`. Applied versions are recorded in a
// `schema_migrations` bookkeeping table; each pending migration runs inside its
// own transaction, so a failure leaves the database in a consistent state at
// the last successfully applied version.
class MigrationRunner {
  public:
    MigrationRunner(ConnectionPool& pool, std::filesystem::path migrations_dir);

    // Runs all pending migrations, returning the number applied. Throws
    // rtc::errors::DatabaseException on any failure.
    int run();

  private:
    void ensure_bookkeeping_table();
    [[nodiscard]] bool is_applied(const std::string& version);
    void apply(const std::string& version, const std::string& sql);

    ConnectionPool& pool_;
    std::filesystem::path migrations_dir_;
};

}  // namespace rtc::database
