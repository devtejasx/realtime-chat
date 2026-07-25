#include "rtc/jobs/background_executor.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace {

using rtc::jobs::BackgroundExecutor;
using namespace std::chrono_literals;

TEST(BackgroundExecutorTest, RunsSubmittedTasks) {
    BackgroundExecutor executor(2);
    executor.start();

    std::atomic<int> counter{0};
    for (int i = 0; i < 50; ++i) {
        executor.submit([&counter] { counter.fetch_add(1); });
    }
    // Drain by stopping (joins workers after the queue empties).
    executor.stop();
    EXPECT_EQ(counter.load(), 50);
    EXPECT_EQ(executor.completed(), 50U);
}

TEST(BackgroundExecutorTest, IsolatesTaskExceptions) {
    BackgroundExecutor executor(1);
    executor.start();
    std::atomic<bool> ran_after{false};
    executor.submit([] { throw std::runtime_error("boom"); });
    executor.submit([&ran_after] { ran_after.store(true); });
    executor.stop();
    EXPECT_TRUE(ran_after.load());  // worker survived the exception
    EXPECT_EQ(executor.failed(), 1U);
}

TEST(BackgroundExecutorTest, DropsSubmissionsWhenStopped) {
    BackgroundExecutor executor(1);
    std::atomic<int> counter{0};
    executor.submit([&counter] { counter.fetch_add(1); });  // not started
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(counter.load(), 0);
}

}  // namespace
