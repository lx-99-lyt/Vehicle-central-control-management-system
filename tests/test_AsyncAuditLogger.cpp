#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "AsyncAuditLogger.hpp"

TEST(AsyncAuditLogger, Singleton) {
    auto& a = AsyncAuditLogger::getInstance();
    auto& b = AsyncAuditLogger::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(AsyncAuditLogger, EnqueueNonBlocking) {
    auto& logger = AsyncAuditLogger::getInstance();

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        logger.enqueue("TEST", "test_action", 10.0f, "test");
    }
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 100条日志投递应在100ms内完成
    EXPECT_LT(ms, 100);
}

TEST(AsyncAuditLogger, QueueDropOld) {
    auto& logger = AsyncAuditLogger::getInstance();

    for (int i = 0; i < 20000; ++i) {
        logger.enqueue("TEST", "overflow_test", static_cast<float>(i), "overflow");
    }

    // 队列大小不应超过 max_queue_size（默认10000）
    EXPECT_LE(logger.queueSize(), 10000);
}

TEST(AsyncAuditLogger, ShutdownWithoutInit) {
    // shutdown 不应崩溃，即使没有 init
    AsyncAuditLogger::getInstance().shutdown();
}
