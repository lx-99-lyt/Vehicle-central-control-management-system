#include <gtest/gtest.h>
#include "Car_Log.hpp"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>

class LoggerTest : public ::testing::Test {
protected:
    static constexpr const char* TEST_LOG = "./test_log.txt";

    void SetUp() override {
        std::filesystem::remove(TEST_LOG);
        // 每个测试重新初始化 logger
        Logger::getInstance().init(TEST_LOG, LogLevel::DEBUG);
    }

    void TearDown() override {
        std::filesystem::remove(TEST_LOG);
        // 清理可能的 rotate 文件
        for (int i = 1; i <= 6; ++i) {
            std::filesystem::remove(std::string(TEST_LOG) + "." + std::to_string(i));
        }
        std::filesystem::remove(std::string(TEST_LOG) + ".old.1");
    }

    std::string readLogFile() {
        std::ifstream in(TEST_LOG);
        if (!in.is_open()) return "";
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
};

// ========== 基本日志功能 ==========

TEST_F(LoggerTest, InitCreatesFile) {
    // init 在 SetUp 中已调用，文件应该已创建
    // 写一条日志确保文件被创建
    LOG_INFO("test message");
    EXPECT_TRUE(std::filesystem::exists(TEST_LOG));
}

TEST_F(LoggerTest, InfoLevelLogged) {
    LOG_INFO("hello %s", "world");
    auto content = readLogFile();
    EXPECT_NE(content.find("INFO"), std::string::npos);
    EXPECT_NE(content.find("hello world"), std::string::npos);
}

TEST_F(LoggerTest, DebugLevelLogged) {
    LOG_DEBUG("debug msg %d", 42);
    auto content = readLogFile();
    EXPECT_NE(content.find("DEBUG"), std::string::npos);
    EXPECT_NE(content.find("debug msg 42"), std::string::npos);
}

TEST_F(LoggerTest, WarnLevelLogged) {
    LOG_WARN("warning %f", 3.14);
    auto content = readLogFile();
    EXPECT_NE(content.find("WARN"), std::string::npos);
}

TEST_F(LoggerTest, ErrorLevelLogged) {
    LOG_ERROR("error code %d", 500);
    auto content = readLogFile();
    EXPECT_NE(content.find("ERROR"), std::string::npos);
    EXPECT_NE(content.find("error code 500"), std::string::npos);
}

// ========== 日志级别过滤 ==========

TEST_F(LoggerTest, DebugFilterHidesDebug) {
    // 重新初始化为 INFO 级别
    Logger::getInstance().init(TEST_LOG, LogLevel::INFO);
    LOG_DEBUG("should not appear");
    LOG_INFO("should appear");

    auto content = readLogFile();
    EXPECT_EQ(content.find("should not appear"), std::string::npos);
    EXPECT_NE(content.find("should appear"), std::string::npos);
}

TEST_F(LoggerTest, WarnFilterHidesDebugAndInfo) {
    Logger::getInstance().init(TEST_LOG, LogLevel::WARN);
    LOG_DEBUG("hidden");
    LOG_INFO("hidden");
    LOG_WARN("visible warn");
    LOG_ERROR("visible error");

    auto content = readLogFile();
    EXPECT_EQ(content.find("hidden"), std::string::npos);
    EXPECT_NE(content.find("visible warn"), std::string::npos);
    EXPECT_NE(content.find("visible error"), std::string::npos);
}

TEST_F(LoggerTest, ErrorFilterOnlyShowsErrors) {
    Logger::getInstance().init(TEST_LOG, LogLevel::ERROR);
    LOG_DEBUG("no");
    LOG_INFO("no");
    LOG_WARN("no");
    LOG_ERROR("yes");

    auto content = readLogFile();
    EXPECT_EQ(content.find("no"), std::string::npos);
    EXPECT_NE(content.find("yes"), std::string::npos);
}

// ========== 日志格式 ==========

TEST_F(LoggerTest, LogContainsTimestamp) {
    LOG_INFO("timestamp test");
    auto content = readLogFile();
    // 格式: [YYYY-MM-DD HH:MM:SS]
    EXPECT_NE(content.find("[20"), std::string::npos);  // 年份以 20 开头
    EXPECT_NE(content.find("-"), std::string::npos);
    EXPECT_NE(content.find(":"), std::string::npos);
}

TEST_F(LoggerTest, LogContainsSourceFile) {
    LOG_INFO("source test");
    auto content = readLogFile();
    // 应包含源文件名
    EXPECT_NE(content.find("test_Logger.cpp"), std::string::npos);
}

TEST_F(LoggerTest, LogContainsLineNumber) {
    LOG_INFO("line test");
    auto content = readLogFile();
    // 应包含行号（数字）
    EXPECT_NE(content.find("line test"), std::string::npos);
}

// ========== 多次写入 ==========

TEST_F(LoggerTest, MultipleLogEntries) {
    LOG_INFO("first");
    LOG_INFO("second");
    LOG_INFO("third");

    auto content = readLogFile();
    EXPECT_NE(content.find("first"), std::string::npos);
    EXPECT_NE(content.find("second"), std::string::npos);
    EXPECT_NE(content.find("third"), std::string::npos);

    // 统计行数（每条日志一行）
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find('\n', pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    EXPECT_GE(count, 3);
}

// ========== 格式化参数 ==========

TEST_F(LoggerTest, FormatStringInt) {
    LOG_INFO("value: %d", 42);
    auto content = readLogFile();
    EXPECT_NE(content.find("value: 42"), std::string::npos);
}

TEST_F(LoggerTest, FormatStringFloat) {
    LOG_INFO("pi: %.2f", 3.14);
    auto content = readLogFile();
    EXPECT_NE(content.find("pi: 3.14"), std::string::npos);
}

TEST_F(LoggerTest, FormatStringMultiple) {
    LOG_INFO("a=%d b=%s c=%.1f", 1, "test", 2.5);
    auto content = readLogFile();
    EXPECT_NE(content.find("a=1 b=test c=2.5"), std::string::npos);
}

// ========== init 重新初始化 ==========

TEST_F(LoggerTest, ReinitChangesLevel) {
    LOG_DEBUG("debug1");

    // 重新初始化为 ERROR
    Logger::getInstance().init(TEST_LOG, LogLevel::ERROR);
    LOG_DEBUG("debug2");
    LOG_ERROR("error1");

    auto content = readLogFile();
    EXPECT_NE(content.find("debug1"), std::string::npos);
    EXPECT_EQ(content.find("debug2"), std::string::npos);
    EXPECT_NE(content.find("error1"), std::string::npos);
}
