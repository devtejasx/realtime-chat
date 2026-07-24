#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <pqxx/transaction>

#include "rtc/config/config.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/database/migration_runner.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/repositories/pg_conversation_repository.hpp"
#include "rtc/repositories/pg_message_repository.hpp"
#include "rtc/repositories/pg_user_repository.hpp"
#include "rtc/utils/env.hpp"

namespace {

// End-to-end persistence test across the Phase 2 repositories against a real
// PostgreSQL instance. Opt-in via RTC_RUN_DB_TESTS=1; skipped otherwise so the
// default suite needs no database.
class MessagingIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (rtc::utils::get_env_or("RTC_RUN_DB_TESTS", "0") != "1") {
            GTEST_SKIP() << "Set RTC_RUN_DB_TESTS=1 to run database integration tests";
        }
        try {
            const auto config = rtc::config::Config::load_from_env();
            pool_ = std::make_unique<rtc::database::ConnectionPool>(
                config.database_connection_string(), 2);
            rtc::database::MigrationRunner(
                *pool_, rtc::utils::get_env_or("MIGRATIONS_DIR", "migrations"))
                .run();
        } catch (const std::exception& ex) {
            GTEST_SKIP() << "Database unavailable: " << ex.what();
        }
        users_ = std::make_unique<rtc::repositories::PgUserRepository>(*pool_);
        conversations_ = std::make_unique<rtc::repositories::PgConversationRepository>(*pool_);
        messages_ = std::make_unique<rtc::repositories::PgMessageRepository>(*pool_);
        suffix_ = std::to_string(std::time(nullptr));
    }

    void TearDown() override {
        if (pool_) {
            auto lease = pool_->acquire();
            pqxx::work txn(lease.get());
            txn.exec_params("DELETE FROM users WHERE username LIKE $1", "itmsg_%");
            txn.commit();
        }
    }

    std::int64_t make_user(const std::string& tag) {
        return users_
            ->create({"itmsg_" + tag + "_" + suffix_,
                      "itmsg_" + tag + "_" + suffix_ + "@example.com", "hash"})
            .id;
    }

    std::unique_ptr<rtc::database::ConnectionPool> pool_;
    std::unique_ptr<rtc::repositories::PgUserRepository> users_;
    std::unique_ptr<rtc::repositories::PgConversationRepository> conversations_;
    std::unique_ptr<rtc::repositories::PgMessageRepository> messages_;
    std::string suffix_;
};

TEST_F(MessagingIntegrationTest, DirectConversationIsDeduplicated) {
    const auto a = make_user("a");
    const auto b = make_user("b");
    const auto first = conversations_->create_or_get_direct(a, b);
    const auto second = conversations_->create_or_get_direct(b, a);
    EXPECT_EQ(first.id, second.id);
}

TEST_F(MessagingIntegrationTest, SendAndListMessages) {
    const auto a = make_user("a");
    const auto b = make_user("b");
    const auto conversation = conversations_->create_or_get_direct(a, b);

    messages_->create({conversation.id, a, "hello", rtc::models::MessageType::kText});
    messages_->create({conversation.id, b, "hi there", rtc::models::MessageType::kText});

    rtc::repositories::MessageFilter filter;
    filter.conversation_id = conversation.id;
    const auto listed = messages_->list(filter, rtc::dto::Pagination{});
    EXPECT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed.front().content, "hi there");  // newest first
}

TEST_F(MessagingIntegrationTest, KeywordSearchUsesFullText) {
    const auto a = make_user("a");
    const auto b = make_user("b");
    const auto conversation = conversations_->create_or_get_direct(a, b);
    messages_->create({conversation.id, a, "the quick brown fox", rtc::models::MessageType::kText});
    messages_->create({conversation.id, a, "lazy dog sleeps", rtc::models::MessageType::kText});

    rtc::repositories::MessageFilter filter;
    filter.conversation_id = conversation.id;
    filter.keyword = "quick";
    EXPECT_EQ(messages_->list(filter, rtc::dto::Pagination{}).size(), 1U);
}

}  // namespace
