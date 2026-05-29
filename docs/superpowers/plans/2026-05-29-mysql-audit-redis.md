# MySQL 异步审计日志 + Redis 状态管理器 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为车载中控系统添加异步 MySQL 审计日志落盘和 Redis 全局状态管理功能

**Architecture:** 两个模块均作为 car_main 进程内的单例运行。AsyncAuditLogger 使用生产者-消费者模型（std::deque + mutex + condition_variable），后台单线程负责 MySQL INSERT。RedisManager 使用 hiredis 封装，支持读写分离，安全关键查询绕过 Redis 直接走 UDS。

**Tech Stack:** C++17, libmysqlclient (MySQL C API), hiredis, nlohmann/json, GoogleTest, CMake

---

## File Structure

```
conf/
  db.conf                     ← MySQL 连接 + 队列配置
  redis.conf                  ← Redis 连接 + 重连配置

include/
  AsyncAuditLogger.hpp        ← 异步审计日志单例头文件
  RedisManager.hpp            ← Redis 状态管理器单例头文件

source/
  AsyncAuditLogger.cpp        ← 异步审计日志实现
  RedisManager.cpp            ← Redis 状态管理器实现

tests/
  test_AsyncAuditLogger.cpp   ← 异步审计日志单元测试
  test_RedisManager.cpp       ← Redis 状态管理器单元测试
```

---

### Task 1: 创建配置文件

**Files:**
- Create: `conf/db.conf`
- Create: `conf/redis.conf`

- [ ] **Step 1: 创建 db.conf**

```conf
# MySQL 连接配置
mysql_host = 127.0.0.1
mysql_port = 3306
mysql_user = root
mysql_password = your_password
mysql_database = car_system
mysql_charset = utf8mb4

# 异步队列配置
audit_queue_max_size = 10000

# 重连配置（毫秒）
reconnect_base_interval_ms = 1000
reconnect_max_interval_ms = 30000
```

- [ ] **Step 2: 创建 redis.conf**

```conf
# Redis 连接配置
host = 127.0.0.1
port = 6379
password =
db = 0
connect_timeout_ms = 5000

# 重连配置（毫秒）
reconnect_base_interval_ms = 1000
reconnect_max_interval_ms = 30000
```

- [ ] **Step 3: Commit**

```bash
git add conf/db.conf conf/redis.conf
git commit -m "feat: 添加 MySQL 和 Redis 配置文件模板"
```

---

### Task 2: 实现 AsyncAuditLogger 头文件

**Files:**
- Create: `include/AsyncAuditLogger.hpp`

- [ ] **Step 1: 编写 AsyncAuditLogger.hpp**

```cpp
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

    // 初始化：读取配置文件，连接 MySQL，启动后台线程
    void init(const std::string& conf_path);

    // 非阻塞投递日志条目
    void enqueue(const std::string& event_type,
                 const std::string& action,
                 float speed,
                 const std::string& reason);

    // 优雅退出：等待队列消费完毕
    void shutdown();

    // 测试用：获取当前队列大小
    size_t queueSize() const;

private:
    AsyncAuditLogger() = default;
    ~AsyncAuditLogger();

    // 禁止拷贝
    AsyncAuditLogger(const AsyncAuditLogger&) = delete;
    AsyncAuditLogger& operator=(const AsyncAuditLogger&) = delete;

    // 配置解析
    void parseConfig(const std::string& conf_path);

    // 后台线程主函数
    void workerLoop();

    // MySQL 操作
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
```

- [ ] **Step 2: Commit**

```bash
git add include/AsyncAuditLogger.hpp
git commit -m "feat: 添加 AsyncAuditLogger 头文件"
```

---

### Task 3: 实现 AsyncAuditLogger 源文件

**Files:**
- Create: `source/AsyncAuditLogger.cpp`

- [ ] **Step 1: 编写 AsyncAuditLogger.cpp**

```cpp
#include "AsyncAuditLogger.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <mysql/mysql.h>

AsyncAuditLogger& AsyncAuditLogger::getInstance() {
    static AsyncAuditLogger instance;
    return instance;
}

AsyncAuditLogger::~AsyncAuditLogger() {
    shutdown();
}

void AsyncAuditLogger::parseConfig(const std::string& conf_path) {
    std::ifstream file(conf_path);
    if (!file.is_open()) {
        std::cerr << "[AsyncAuditLogger] Config file not found: " << conf_path
                  << ", using defaults\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 跳过注释和空行
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // trim
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(val);

        if (key == "mysql_host") m_host = val;
        else if (key == "mysql_port") m_port = static_cast<uint16_t>(std::stoi(val));
        else if (key == "mysql_user") m_user = val;
        else if (key == "mysql_password") m_password = val;
        else if (key == "mysql_database") m_database = val;
        else if (key == "mysql_charset") m_charset = val;
        else if (key == "audit_queue_max_size") m_max_queue_size = static_cast<size_t>(std::stoul(val));
        else if (key == "reconnect_base_interval_ms") m_reconnect_base_ms = std::stoi(val);
        else if (key == "reconnect_max_interval_ms") m_reconnect_max_ms = std::stoi(val);
    }
}

bool AsyncAuditLogger::connectMySQL() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) return false;

    mysql_options(conn, MYSQL_SET_CHARSET_NAME, m_charset.c_str());

    if (!mysql_real_connect(conn,
                            m_host.c_str(),
                            m_user.c_str(),
                            m_password.c_str(),
                            m_database.c_str(),
                            m_port,
                            nullptr, 0)) {
        std::cerr << "[AsyncAuditLogger] MySQL connect failed: "
                  << mysql_error(conn) << "\n";
        mysql_close(conn);
        return false;
    }

    m_mysql = conn;
    m_connected = true;
    return true;
}

bool AsyncAuditLogger::reconnect() {
    if (m_mysql) {
        mysql_close(static_cast<MYSQL*>(m_mysql));
        m_mysql = nullptr;
    }
    m_connected = false;
    return connectMySQL();
}

bool AsyncAuditLogger::insertLog(const AuditLogEntry& entry) {
    if (!m_connected || !m_mysql) return false;

    MYSQL* conn = static_cast<MYSQL*>(m_mysql);

    // 检查连接是否存活
    if (mysql_ping(conn) != 0) {
        m_connected = false;
        return false;
    }

    // 构造 INSERT 语句
    std::string sql = "INSERT INTO audit_logs (event_type, action, speed, reason) VALUES ('";
    sql += entry.event_type;
    sql += "', '";
    sql += entry.action;
    sql += "', ";
    sql += std::to_string(entry.speed);
    sql += ", '";
    sql += entry.reason;
    sql += "')";

    if (mysql_real_query(conn, sql.c_str(), sql.length()) != 0) {
        std::cerr << "[AsyncAuditLogger] INSERT failed: " << mysql_error(conn) << "\n";
        return false;
    }
    return true;
}

void AsyncAuditLogger::workerLoop() {
    int current_interval = m_reconnect_base_ms;

    while (m_running) {
        // 如果未连接，尝试重连
        if (!m_connected) {
            if (reconnect()) {
                current_interval = m_reconnect_base_ms;  // 重连成功，重置间隔
                std::cout << "[AsyncAuditLogger] MySQL reconnected\n";
            } else {
                // 指数退避
                std::this_thread::sleep_for(std::chrono::milliseconds(current_interval));
                current_interval = std::min(current_interval * 2, m_reconnect_max_ms);
                continue;
            }
        }

        // 等待队列中有数据
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });

        if (!m_running && m_queue.empty()) break;

        // 取出所有待处理条目
        std::deque<AuditLogEntry> batch;
        batch.swap(m_queue);
        lock.unlock();

        // 批量写入
        for (size_t i = 0; i < batch.size(); ++i) {
            if (!insertLog(batch[i])) {
                // 写入失败，把剩余条目重新入队
                std::lock_guard<std::mutex> qlock(m_queue_mutex);
                m_queue.insert(m_queue.begin(), batch.begin() + static_cast<long>(i), batch.end());
                break;
            }
        }
    }
}

void AsyncAuditLogger::init(const std::string& conf_path) {
    if (m_running) return;  // 已初始化

    parseConfig(conf_path);

    // 尝试连接 MySQL（非阻塞，失败不阻塞主程序）
    connectMySQL();

    m_running = true;
    m_worker = std::thread(&AsyncAuditLogger::workerLoop, this);
}

void AsyncAuditLogger::enqueue(const std::string& event_type,
                                const std::string& action,
                                float speed,
                                const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_queue_mutex);

    // 队列满时丢弃最旧条目
    if (m_queue.size() >= m_max_queue_size) {
        m_queue.pop_front();
    }

    m_queue.push_back({event_type, action, speed, reason});
    m_cv.notify_one();
}

void AsyncAuditLogger::shutdown() {
    if (!m_running) return;

    m_running = false;
    m_cv.notify_one();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    // 关闭 MySQL 连接
    if (m_mysql) {
        mysql_close(static_cast<MYSQL*>(m_mysql));
        m_mysql = nullptr;
    }
    m_connected = false;
}

size_t AsyncAuditLogger::queueSize() const {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    return m_queue.size();
}
```

- [ ] **Step 2: Commit**

```bash
git add source/AsyncAuditLogger.cpp
git commit -m "feat: 实现 AsyncAuditLogger 异步审计日志模块"
```

---

### Task 4: 实现 RedisManager 头文件

**Files:**
- Create: `include/RedisManager.hpp`

- [ ] **Step 1: 编写 RedisManager.hpp**

```cpp
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

    // 初始化：读取配置文件，连接 Redis
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

    // 关闭连接
    void shutdown();

private:
    RedisManager() = default;
    ~RedisManager();

    // 禁止拷贝
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;

    // 配置解析
    void parseConfig(const std::string& conf_path);

    // 连接管理
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
```

- [ ] **Step 2: Commit**

```bash
git add include/RedisManager.hpp
git commit -m "feat: 添加 RedisManager 头文件"
```

---

### Task 5: 实现 RedisManager 源文件

**Files:**
- Create: `source/RedisManager.cpp`

- [ ] **Step 1: 编写 RedisManager.cpp**

```cpp
#include "RedisManager.hpp"
#include <hiredis/hiredis.h>
#include <fstream>
#include <sstream>
#include <iostream>

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
    tv.tv_usec = (m_connect_timeout_ms % 1000) * 1000;

    redisContext* ctx = redisConnectWithTimeout(m_host.c_str(), m_port, tv);
    if (!ctx || ctx->err) {
        if (ctx) {
            std::cerr << "[RedisManager] Connect failed: " << ctx->errstr << "\n";
            redisFree(ctx);
        }
        m_connected = false;
        return false;
    }

    // 认证
    if (!m_password.empty()) {
        redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "AUTH %s", m_password.c_str()));
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::cerr << "[RedisManager] AUTH failed\n";
            if (reply) freeReplyObject(reply);
            redisFree(ctx);
            m_connected = false;
            return false;
        }
        freeReplyObject(reply);
    }

    // 选择数据库
    if (m_db != 0) {
        redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "SELECT %d", m_db));
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
        // 检查连接是否存活
        redisContext* ctx = static_cast<redisContext*>(m_context);
        redisReply* reply = static_cast<redisReply*>(redisCommand(ctx, "PING"));
        if (reply) {
            bool ok = (reply->type == REDIS_REPLY_STATUS &&
                       std::string(reply->str) == "PONG");
            freeReplyObject(reply);
            if (ok) return true;
        }
        // 连接已断开
        m_connected = false;
    }

    // 指数退避重连
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
    if (!ensureConnected()) return;  // 重连失败，静默丢弃

    redisContext* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
    if (reply) freeReplyObject(reply);
}

void RedisManager::updateModule(const std::string& module,
                                 const std::unordered_map<std::string, std::string>& fields) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return;

    redisContext* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    for (const auto& [field, value] : fields) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
        if (reply) freeReplyObject(reply);
    }
}

std::optional<std::unordered_map<std::string, std::string>>
RedisManager::GetModuleStatus(const std::string& module) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!ensureConnected()) return std::nullopt;

    redisContext* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    redisReply* reply = static_cast<redisReply*>(
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

    redisContext* ctx = static_cast<redisContext*>(m_context);
    std::string key = "car:" + module;

    redisReply* reply = static_cast<redisReply*>(
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
```

- [ ] **Step 2: Commit**

```bash
git add source/RedisManager.cpp
git commit -m "feat: 实现 RedisManager 状态管理器模块"
```

---

### Task 6: 更新 CMakeLists.txt 集成新模块

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 添加 MySQL 和 hiredis 依赖，更新编译目标**

在 `CMakeLists.txt` 的 `find_package(CURL QUIET)` 之前添加：

```cmake
# ========== MySQL ==========
find_library(MYSQL_LIBRARY mysqlclient)
if(MYSQL_LIBRARY)
    message(STATUS "MySQL client found: ${MYSQL_LIBRARY}")
else()
    message(WARNING "libmysqlclient not found. Install: sudo apt install libmysqlclient-dev")
endif()

# ========== hiredis ==========
include(FetchContent)
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(hiredis)
```

更新 `car_main` 目标：

```cmake
add_executable(car_main
    source/main.cpp
    source/Car_Log.cpp
    source/ConfigManager.cpp
    source/AsyncAuditLogger.cpp
    source/RedisManager.cpp
)

target_link_libraries(car_main PRIVATE nlohmann_json::nlohmann_json)
if(MYSQL_LIBRARY)
    target_link_libraries(car_main PRIVATE ${MYSQL_LIBRARY})
    target_compile_definitions(car_main PRIVATE HAS_MYSQL=1)
endif()
target_link_libraries(car_main PRIVATE hiredis)
```

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: CMakeLists 集成 MySQL 和 Redis 依赖"
```

---

### Task 7: 更新 main.cpp 初始化新模块

**Files:**
- Modify: `source/main.cpp`

- [ ] **Step 1: 添加头文件引用**

在 `source/main.cpp` 顶部添加：

```cpp
#include "AsyncAuditLogger.hpp"
#include "RedisManager.hpp"
```

- [ ] **Step 2: 在 main() 中初始化模块**

在 `Logger::getInstance().init(...)` 之后添加：

```cpp
// 初始化审计日志（MySQL 断连不阻塞主程序）
#ifdef HAS_MYSQL
AsyncAuditLogger::getInstance().init("conf/db.conf");
LOG_INFO("AsyncAuditLogger initialized");
#endif

// 初始化 Redis 状态管理器
RedisManager::getInstance().init("conf/redis.conf");
LOG_INFO("RedisManager initialized");
```

- [ ] **Step 3: 在 applyAutoLockRule() 中添加审计日志**

修改 `applyAutoLockRule()` 函数，在自动落锁成功后添加审计日志：

```cpp
if (sendRequest(Car::SOCK_DOOR, req, resp) && resp.result == 0) {
    LOG_INFO("Auto lock applied at speed %.1f km/h", static_cast<double>(status.speed));
    AUDIT_LOG("NORMAL_OP", "auto_lock", status.speed, "车速超阈值自动落锁");
}
```

- [ ] **Step 4: 在进程退出时优雅关闭**

在 `main()` 的 `syncAndSaveConfig()` 之前添加：

```cpp
// 优雅关闭审计日志（等待队列消费完毕）
#ifdef HAS_MYSQL
AsyncAuditLogger::getInstance().shutdown();
#endif
RedisManager::getInstance().shutdown();
```

- [ ] **Step 5: Commit**

```bash
git add source/main.cpp
git commit -m "feat: main.cpp 集成 AsyncAuditLogger 和 RedisManager"
```

---

### Task 8: 编写 AsyncAuditLogger 单元测试

**Files:**
- Create: `tests/test_AsyncAuditLogger.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 编写测试文件**

```cpp
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

    // 投递应立即返回（非阻塞）
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
    // 使用一个独立实例测试队列满时的行为
    // 注意：这里测试的是 enqueue 的非阻塞特性
    auto& logger = AsyncAuditLogger::getInstance();

    // 快速投递大量日志，不应阻塞或崩溃
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
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加测试**

在 `car_tests` 之后添加：

```cmake
add_executable(test_async_audit_logger
    tests/test_AsyncAuditLogger.cpp
    source/AsyncAuditLogger.cpp
)
target_link_libraries(test_async_audit_logger PRIVATE GTest::gtest GTest::gtest_main)
if(MYSQL_LIBRARY)
    target_link_libraries(test_async_audit_logger PRIVATE ${MYSQL_LIBRARY})
    target_compile_definitions(test_async_audit_logger PRIVATE HAS_MYSQL=1)
endif()
target_include_directories(test_async_audit_logger PRIVATE ${CMAKE_SOURCE_DIR}/include)

gtest_discover_tests(test_async_audit_logger)
```

- [ ] **Step 3: 运行测试**

```bash
cd build && cmake .. && make test_async_audit_logger
./bin/test_async_audit_logger
```

预期：所有测试 PASS（注意：MySQL 连接测试会因无数据库而跳过，这是预期行为）

- [ ] **Step 4: Commit**

```bash
git add tests/test_AsyncAuditLogger.cpp CMakeLists.txt
git commit -m "feat: 添加 AsyncAuditLogger 单元测试"
```

---

### Task 9: 编写 RedisManager 单元测试

**Files:**
- Create: `tests/test_RedisManager.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 编写测试文件**

```cpp
#include <gtest/gtest.h>
#include "RedisManager.hpp"

TEST(RedisManager, Singleton) {
    auto& a = RedisManager::getInstance();
    auto& b = RedisManager::getInstance();
    EXPECT_EQ(&a, &b);
}

TEST(RedisManager, GetFieldWithoutConnection) {
    // 未连接时应返回 nullopt，不崩溃
    auto result = RedisManager::getInstance().GetField("door", "front_left");
    EXPECT_FALSE(result.has_value());
}

TEST(RedisManager, GetAllStatusWithoutConnection) {
    // 未连接时应返回 nullopt
    auto result = RedisManager::getInstance().GetAllStatus();
    EXPECT_FALSE(result.has_value());
}

TEST(RedisManager, UpdateWithoutConnection) {
    // 未连接时不应崩溃（静默丢弃）
    RedisManager::getInstance().update("door", "front_left", "1");
    // 如果没有崩溃就算通过
    SUCCEED();
}

TEST(RedisManager, ShutdownWithoutInit) {
    // shutdown 不应崩溃
    RedisManager::getInstance().shutdown();
    SUCCEED();
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加测试**

```cmake
add_executable(test_redis_manager
    tests/test_RedisManager.cpp
    source/RedisManager.cpp
)
target_link_libraries(test_redis_manager PRIVATE GTest::gtest GTest::gtest_main nlohmann_json::nlohmann_json hiredis)
target_include_directories(test_redis_manager PRIVATE ${CMAKE_SOURCE_DIR}/include)

gtest_discover_tests(test_redis_manager)
```

- [ ] **Step 3: 运行测试**

```bash
cd build && cmake .. && make test_redis_manager
./bin/test_redis_manager
```

预期：所有测试 PASS（无 Redis 时连接测试会优雅降级）

- [ ] **Step 4: Commit**

```bash
git add tests/test_RedisManager.cpp CMakeLists.txt
git commit -m "feat: 添加 RedisManager 单元测试"
```

---

### Task 10: 添加数据库初始化脚本

**Files:**
- Create: `conf/init_db.sql`

- [ ] **Step 1: 编写建表 SQL**

```sql
-- 审计日志表
CREATE DATABASE IF NOT EXISTS car_system DEFAULT CHARSET utf8mb4;
USE car_system;

CREATE TABLE IF NOT EXISTS audit_logs (
    id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    timestamp   DATETIME(3)    NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    event_type  VARCHAR(32)    NOT NULL COMMENT '事件类型: AI_INTERCEPT / NORMAL_OP',
    action      VARCHAR(128)   NOT NULL COMMENT '具体动作',
    speed       FLOAT          NOT NULL DEFAULT 0 COMMENT '当前车速 km/h',
    reason      VARCHAR(256)   NOT NULL DEFAULT '' COMMENT '拦截原因',
    INDEX idx_timestamp (timestamp),
    INDEX idx_event_type (event_type)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

- [ ] **Step 2: Commit**

```bash
git add conf/init_db.sql
git commit -m "feat: 添加数据库初始化脚本"
```

---

### Task 11: 最终构建验证

- [ ] **Step 1: 完整构建**

```bash
cd build && cmake .. && make -j$(nproc)
```

预期：所有目标编译成功，无错误

- [ ] **Step 2: 运行所有测试**

```bash
ctest --output-on-failure
```

预期：所有测试 PASS

- [ ] **Step 3: 最终 Commit**

```bash
git add -A
git commit -m "feat: MySQL 异步审计日志 + Redis 状态管理器完整实现"
```
