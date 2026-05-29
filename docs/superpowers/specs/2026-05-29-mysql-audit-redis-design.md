# MySQL 异步审计日志 + Redis 状态管理器 设计文档

**日期**: 2026-05-29
**分支**: main
**架构方案**: 方案 B — car_main 进程内嵌线程

---

## 1. 概述

为车载中控系统新增两个核心模块：

1. **AsyncAuditLogger** — 异步 MySQL 审计日志，非阻塞投递，后台线程落盘
2. **RedisManager** — Redis 全局状态中心，读写分离，安全查询绕过缓存

两个模块均作为 `car_main` 进程内的单例运行，复用现有 Logger/ConfigManager 的单例模式。

---

## 2. MySQL 审计日志模块

### 2.1 数据库表结构

```sql
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

### 2.2 线程模型

```
car_main (Epoll 主循环)
  │
  │  AUDIT_LOG(type, action, speed, reason)  ← 宏，微秒级返回
  │
  ▼
AsyncAuditLogger (单例)
  ┌──────────────────────┐
  │ BoundedQueue          │  std::deque + std::mutex + std::condition_variable
  │ max_size 可配置       │  队列满时 pop_front 丢弃最旧条目
  └──────────┬───────────┘
             │
             ▼
  ┌──────────────────────┐
  │ Background Thread     │  独立单线程
  │ - wait on cv          │
  │ - pop from queue      │
  │ - mysql_real_query    │
  │ - reconnect on fail   │
  └──────────────────────┘
```

### 2.3 接口

```cpp
// 宏定义 — 一行投递
#define AUDIT_LOG(type, action, speed, reason) \
    AsyncAuditLogger::getInstance().enqueue(type, action, speed, reason)

// 类接口
class AsyncAuditLogger {
public:
    static AsyncAuditLogger& getInstance();
    void init(const std::string& conf_path);  // 读取 db.conf，启动后台线程
    void enqueue(const std::string& event_type,
                 const std::string& action,
                 float speed,
                 const std::string& reason);
    void shutdown();  // 优雅退出，等待队列消费完毕
};
```

### 2.4 异常兜底

| 场景 | 策略 |
|------|------|
| 队列满 | Drop old: pop_front 丢弃最旧，push_back 新条目 |
| MySQL 断线 | 指数退避重连: 1s→2s→4s→...→max_interval（可配置） |
| 重连期间 | 日志继续入队，重连成功后正常消费 |
| 进程退出 | shutdown() 等待后台线程消费完剩余队列 |

### 2.5 配置文件 `db.conf`

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

# 重连配置
reconnect_base_interval_ms = 1000
reconnect_max_interval_ms = 30000
```

### 2.6 CMake 依赖

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQL REQUIRED mysqlclient)
target_link_libraries(car_main PRIVATE ${MYSQL_LIBRARIES})
target_include_directories(car_main PRIVATE ${MYSQL_INCLUDE_DIRS})
```

或直接：
```cmake
find_library(MYSQL_LIBRARY mysqlclient)
target_link_libraries(car_main PRIVATE ${MYSQL_LIBRARY})
```

---

## 3. Redis 状态管理器

### 3.1 数据结构

所有模块状态以 Hash 结构存储在 Redis 中，Value 统一转为字符串：

```
KEY: car:door
  front_left  → "0"
  front_right → "0"
  back_left   → "0"
  back_right  → "0"
  trunk       → "0"
  lock_status → "1"

KEY: car:status
  speed            → "25.3"
  rpm              → "2000"
  water_temp       → "85.5"
  oil_temp         → "92.0"
  fuel             → "68.5"
  battery_voltage  → "12.4"
  gear             → "D"
  hand_brake       → "0"

KEY: car:air
  ac_switch   → "1"
  fan_speed   → "3"
  temp_set    → "24"
  inner_cycle → "0"

KEY: car:fault
  fault_count → "0"
  wring_light → "0"
```

### 3.2 读写分离架构

```
car_main
  │
  ├─ 子模块处理完 UDS → RedisManager::update(module, field, value)
  │                      └─ HSET car:{module} {field} {value}
  │
  ├─ 非安全查询 → RedisManager::GetAllStatus() → JSON
  │                └─ HGETALL car:door + car:status + car:air + car:fault
  │
  └─ 安全关键查询（车速落锁等）→ 绕过 Redis，直接 sendRequest() 走 UDS
```

### 3.3 接口

```cpp
class RedisManager {
public:
    static RedisManager& getInstance();
    void init(const std::string& conf_path);  // 读取 redis.conf

    // 写操作 — 子模块更新本地状态后调用
    void update(const std::string& module, const std::string& field, const std::string& value);

    // 读操作 — 非安全查询，返回 JSON
    std::optional<nlohmann::json> GetAllStatus();

    // 读操作 — 单个 Hash 全字段
    std::optional<std::unordered_map<std::string, std::string>> GetModuleStatus(const std::string& module);

    // 读操作 — 单字段
    std::optional<std::string> GetField(const std::string& module, const std::string& field);

    void shutdown();
};
```

### 3.4 安全查询的处理

安全关键场景（如车速落锁判断）**不走 RedisManager**，直接调用已有的 `sendRequest()` 函数通过 UDS 获取实时数据：

```cpp
// 在 applyAutoLockRule() 中 — 已有逻辑，不改
Car::StatusState status{};
fetchStateFromModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, &status);
// 直接使用 status.speed，这是从底层获取的权威数据
```

RedisManager 不提供"穿透 UDS"的接口，保持职责单一。

### 3.5 断线重连

- 每次操作前检测连接状态（`redis->err` 或 ping）
- 失败时按可配置间隔指数退避重连
- 重连期间：查询返回 `std::nullopt`，写操作静默丢弃

### 3.6 配置文件 `redis.conf`

```conf
# Redis 连接配置
host = 127.0.0.1
port = 6379
password =
db = 0
connect_timeout_ms = 5000

# 重连配置
reconnect_base_interval_ms = 1000
reconnect_max_interval_ms = 30000
```

### 3.7 CMake 依赖

```cmake
# 使用 FetchContent 获取 hiredis
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(hiredis)

target_link_libraries(car_main PRIVATE hiredis::hiredis)
```

---

## 4. 文件结构

```
include/
  AsyncAuditLogger.hpp    ← 异步审计日志头文件
  RedisManager.hpp        ← Redis 状态管理器头文件

source/
  AsyncAuditLogger.cpp    ← 异步审计日志实现
  RedisManager.cpp        ← Redis 状态管理器实现

conf/
  db.conf                 ← MySQL 连接配置
  redis.conf              ← Redis 连接配置
```

---

## 5. 与现有代码的集成点

| 集成点 | 说明 |
|--------|------|
| `main.cpp` | 初始化 AsyncAuditLogger 和 RedisManager |
| `applyAutoLockRule()` | 车速落锁触发时调用 `AUDIT_LOG()` |
| 子模块 `processCommand()` | WRITE 操作后调用 `RedisManager::update()` |
| `car_ai.cpp` | AI 指令拦截时调用 `AUDIT_LOG()` |
| `syncAndSaveConfig()` | 聚合状态后同步到 Redis |

---

## 6. 依赖汇总

| 依赖 | 用途 | 安装方式 |
|------|------|----------|
| libmysqlclient-dev | MySQL C API | `sudo apt install libmysqlclient-dev` |
| hiredis | Redis C 客户端 | FetchContent（CMake 自动下载） |
| nlohmann/json | JSON 序列化 | 已有（FetchContent） |
