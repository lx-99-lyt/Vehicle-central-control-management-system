#include "RedisManager.hpp"
#include <hiredis/hiredis.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>

RedisManager& RedisManager::getInstance() {
    static RedisManager instance;
    return instance;
}

RedisManager::~RedisManager() {
    shutdown();
}

void RedisManager::parseConfig(const std::string& conf_path) {
    std::ifstream file(conf_path);
    if (!file.is_open()) {
        std::cerr << "[RedisManager] Config file not found: " << conf_path
                  << ", using defaults\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(val);

        if (key == "host") m_host = val;
        else if (key == "port") m_port = static_cast<uint16_t>(std::stoi(val));
        else if (key == "password") m_password = val;
        else if (key == "db") m_db = std::stoi(val);
        else if (key == "connect_timeout_ms") m_connect_timeout_ms = std::stoi(val);
        else if (key == "reconnect_base_interval_ms") m_reconnect_base_ms = std::stoi(val);
        else if (key == "reconnect_max_interval_ms") m_reconnect_max_ms = std::stoi(val);
    }
}

bool RedisManager::connect() {
    if (m_context) {
        redisFree(static_cast<redisContext*>(m_context));
        m_context = nullptr;
    }

    struct timeval tv;
    tv.tv_sec = m_connect_timeout_ms / 1000;
    tv.tv_usec = static_cast<long>(m_connect_timeout_ms % 1000) * 1000;

    auto* ctx = redisConnectWithTimeout(m_host.c_str(), m_port, tv);
    if (!ctx || ctx->err) {
        if (ctx) {
            std::cerr << "[RedisManager] Connect failed: " << ctx->errstr << "\n";
            redisFree(ctx);
        }
        m_connected = false;
        return false;
    }

    if (!m_password.empty()) {
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "AUTH %s", m_password.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::cerr << "[RedisManager] AUTH failed\n";
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
            m_connected = false;
            return false;
        }
        freeReplyObject(reply);
    }

    if (m_db != 0) {
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "SELECT %d", m_db));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::cerr << "[RedisManager] SELECT db failed\n";
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
            m_connected = false;
            return false;
        }
        freeReplyObject(reply);
    }

    m_context = ctx;
    m_connected = true;
    m_current_reconnect_ms = m_reconnect_base_ms;
    return true;
}

bool RedisManager::ensureConnected() {
    if (m_connected && m_context) {
        auto* ctx = static_cast<redisContext*>(m_context);
        auto* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
        if (reply) {
            bool ok = (reply->type == REDIS_REPLY_STATUS &&
                       std::string(reply->str) == "PONG");
            freeReplyObject(reply);
            if (ok) return true;
        }
        m_connected = false;
    }

    if (connect()) return true;

    std::this_thread::sleep_for(std::chrono::milliseconds(m_current_reconnect_ms));
    m_current_reconnect_ms = std::min(m_current_reconnect_ms * 2, m_reconnect_max_ms);
    return false;
}

void RedisManager::init(const std::string& conf_path) {
    parseConfig(conf_path);
    connect();
}

void RedisManager::update(const std::string& module, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return;

    auto* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
    if (reply) freeReplyObject(reply);
}

void RedisManager::updateModule(const std::string& module,
                                 const std::unordered_map<std::string, std::string>& fields) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return;

    auto* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    for (const auto& [field, value] : fields) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
        if (reply) freeReplyObject(reply);
    }
}

std::optional<std::unordered_map<std::string, std::string>>
RedisManager::GetModuleStatus(const std::string& module) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return std::nullopt;

    auto* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "HGETALL %s", key.c_str()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> result;
    for (size_t i = 0; i + 1 < reply->elements; i += 2) {
        result[reply->element[i]->str] = reply->element[i + 1]->str;
    }
    freeReplyObject(reply);
    return result;
}

std::optional<std::string>
RedisManager::GetField(const std::string& module, const std::string& field) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return std::nullopt;

    auto* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "HGET %s %s", key.c_str(), field.c_str()));
    if (!reply || reply->type == REDIS_REPLY_ERROR || reply->type == REDIS_REPLY_NIL) {
        if (reply) freeReplyObject(reply);
        return std::nullopt;
    }

    std::string value = reply->str ? reply->str : "";
    freeReplyObject(reply);
    return value;
}

std::optional<nlohmann::json> RedisManager::GetAllStatus() {
    static const std::vector<std::string> modules = {"door", "status", "air", "fault"};
    nlohmann::json result;

    for (const auto& module : modules) {
        auto status = GetModuleStatus(module);
        if (!status) return std::nullopt;
        result[module] = *status;
    }
    return result;
}

void RedisManager::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_context) {
        redisFree(static_cast<redisContext*>(m_context));
        m_context = nullptr;
    }
    m_connected = false;
}
