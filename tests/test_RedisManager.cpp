#include <gtest/gtest.h>
#include "RedisManager.hpp"

TEST(RedisManager, Singleton) {
    auto& a = RedisManager::getInstance();
    auto& b = RedisManager::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(RedisManager, GetFieldWithoutConnection) {
    auto result = RedisManager::getInstance().GetField("door", "front_left");
    EXPECT_FALSE(result.has_value());
}

TEST(RedisManager, GetAllStatusWithoutConnection) {
    auto result = RedisManager::getInstance().GetAllStatus();
    EXPECT_FALSE(result.has_value());
}

TEST(RedisManager, UpdateWithoutConnection) {
    // 未连接时不应崩溃（静默丢弃）
    RedisManager::getInstance().update("door", "front_left", "1");
    SUCCEED();
}

TEST(RedisManager, ShutdownWithoutInit) {
    RedisManager::getInstance().shutdown();
    SUCCEED();
}
