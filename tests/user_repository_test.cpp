#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <memory>
#include <pqxx/transaction>
#include <string>

#include "rtc/config/config.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/database/migration_runner.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/repositories/pg_user_repository.hpp"
#include "rtc/utils/env.hpp"

namespace {

using rtc::errors::ConflictException;

// Integration test against a real PostgreSQL instance. It is opt-in: set
// RTC_RUN_DB_TESTS=1 (and the usual DB_* env vars) to run it, e.g. via
// docker-compose. When the flag is absent or the database is unreachable, the
// test is skipped so the default `ctest` run stays green without infrastructure.
class UserRepositoryDbTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (rtc::utils::get_env_or("RTC_RUN_DB_TESTS", "0") != "1") {
            GTEST_SKIP() << "Set RTC_RUN_DB_TESTS=1 to run database integration tests";
        }
        try {
            const auto config = rtc::config::Config::load_from_env();
            pool_ = std::make_unique<rtc::database::ConnectionPool>(
                config.database_connection_string(), 2);
            rtc::database::MigrationRunner runner(
                *pool_, rtc::utils::get_env_or("MIGRATIONS_DIR", "migrations"));
            runner.run();
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Database unavailable: " << ex.what();
        }
        repo_ = std::make_unique<rtc::repositories::PgUserRepository>(*pool_);

        // Unique suffix keeps test rows from colliding across runs.
        suffix_ = std::to_string(std::time(nullptr));
    }

    void TearDown() override {
        if (pool_) {
            auto lease = pool_->acquire();
            pqxx::work txn(lease.get());
            txn.exec_params("DELETE FROM users WHERE username LIKE $1", "ittest_%");
            txn.commit();
        }
    }

    rtc::repositories::NewUser new_user(const std::string& tag) {
        return rtc::repositories::NewUser{"ittest_" + tag + "_" + suffix_,
                                          "ittest_" + tag + "_" + suffix_ + "@example.com",
                                          "hashed-password"};
    }

    std::unique_ptr<rtc::database::ConnectionPool> pool_;
    std::unique_ptr<rtc::repositories::PgUserRepository> repo_;
    std::string suffix_;
};

TEST_F(UserRepositoryDbTest, CreateAndFindById) {
    const auto created = repo_->create(new_user("alice"));
    EXPECT_GT(created.id, 0);

    const auto found = repo_->find_by_id(created.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->username, created.username);
}

TEST_F(UserRepositoryDbTest, FindByIdentifierMatchesUsernameOrEmail) {
    const auto created = repo_->create(new_user("bob"));
    EXPECT_TRUE(repo_->find_by_identifier(created.username).has_value());
    EXPECT_TRUE(repo_->find_by_identifier(created.email).has_value());
    EXPECT_FALSE(repo_->find_by_identifier("no_such_user").has_value());
}

TEST_F(UserRepositoryDbTest, DuplicateUsernameThrowsConflict) {
    const auto user = new_user("carol");
    repo_->create(user);
    EXPECT_THROW(repo_->create(user), ConflictException);
}

TEST_F(UserRepositoryDbTest, ExistenceChecks) {
    const auto created = repo_->create(new_user("dave"));
    EXPECT_TRUE(repo_->exists_by_username(created.username));
    EXPECT_TRUE(repo_->exists_by_email(created.email));
    EXPECT_FALSE(repo_->exists_by_username("ittest_absent_" + suffix_));
}

}  // namespace
