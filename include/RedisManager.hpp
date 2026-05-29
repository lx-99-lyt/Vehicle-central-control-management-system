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
    void update(const std::string& module, const std::string& field, const std::string& value);
    void updateModule(const std::string& module, const std::unordered_map<std::string, std::string>& fields);
    std::optional<nlohmann::json> GetAllStatus();
    std::optional<std::unordered_map<std::string, std::string>> GetModuleStatus(const std::string& module);
    std::optional<std::string> GetField(const std::string& module, const std::string& field);
    void shutdown();

    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

private:
    RedisManager() = default;
    ~RedisManager();

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
