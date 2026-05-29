#pragma once
#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdint>

struct AuditLogEntry {
    std::string event_type;   // "AI_INTERCEPT" / "NORMAL_OP"
    std::string action;       // 具体动作
    float speed;              // 当前车速 km/h
    std::string reason;       // 拦截原因
};

class AsyncAuditLogger {
public:
    static AsyncAuditLogger& getInstance();

    void init(const std::string& conf_path);
    void enqueue(const std::string& event_type,
                 const std::string& action,
                 float speed,
                 const std::string& reason);
    void shutdown();
    size_t queueSize() const;

    AsyncAuditLogger(const AsyncAuditLogger&) = delete;
    AsyncAuditLogger& operator=(const AsyncAuditLogger&) = delete;

private:
    AsyncAuditLogger() = default;
    ~AsyncAuditLogger();

    void parseConfig(const std::string& conf_path);
    void workerLoop();

    bool connectMySQL();
    bool insertLog(const AuditLogEntry& entry);
    bool reconnect();

    // 配置
    std::string m_host = "127.0.0.1";
    uint16_t m_port = 3306;
    std::string m_user = "root";
    std::string m_password;
    std::string m_database = "car_system";
    std::string m_charset = "utf8mb4";
    size_t m_max_queue_size = 10000;
    int m_reconnect_base_ms = 1000;
    int m_reconnect_max_ms = 30000;

    // 队列
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::deque<AuditLogEntry> m_queue;

    // 后台线程
    std::thread m_worker;
    std::atomic<bool> m_running{false};

    // MySQL 连接（void* 避免头文件依赖 mysql.h）
    void* m_mysql = nullptr;
    std::atomic<bool> m_connected{false};
};

// 便捷宏
#define AUDIT_LOG(type, action, speed, reason) \
    AsyncAuditLogger::getInstance().enqueue(type, action, speed, reason)
