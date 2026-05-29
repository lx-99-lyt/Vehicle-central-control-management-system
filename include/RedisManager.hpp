#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>

class RedisManager {
public:
    static RedisManager& getInstance();

    void init(const std::string& conf_path);

    // 写操作 — 子模块更新本地状态后调用
    void update(const std::string& module, const std::string& field, const std::string& value);

    // 批量写操作
    void updateModule(const std::string& module, const std::unordered_map<std::string, std::string>& fields);

    // 读操作 — 非安全查询，返回所有模块状态的 JSON
    std::optional<nlohmann::json> GetAllStatus();

    // 读操作 — 单个 Hash 全字段
    std::optional<std::unordered_map<std::string, std::string>> GetModuleStatus(const std::string& module);

    // 读操作 — 单字段
    std::optional<std::string> GetField(const std::string& module, const std::string& field);

    void shutdown();

private:
    RedisManager() = default;
    ~RedisManager();

    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    void parseConfig(const std::string& conf_path);
    bool connect();
    bool ensureConnected();

    // 配置
    std::string m_host = "127.0.0.1";
    uint16_t m_port = 6379;
    std::string m_password;
    int m_db = 0;
    int m_connect_timeout_ms = 5000;
    int m_reconnect_base_ms = 1000;
    int m_reconnect_max_ms = 30000;

    // Redis 连接（void* 避免头文件依赖 hiredis）
    void* m_context = nullptr;
    std::atomic<bool> m_connected{false};
    int m_current_reconnect_ms = 0;

    mutable std::mutex m_mutex;
};
