// car_ai.cpp — 智能车载 AI 大脑进程
//
// 职责：
//   1. 从 car_ai.conf(需指定路径) 读取 API Key / 模型 / 接口地址
//   2. 维护多轮对话历史（最近 MAX_HISTORY 条）
//   3. 用户输入自然语言 → POST 到 DeepSeek API → 解析 JSON 指令
//   4. 安全状态机：高速行驶时拦截危险指令
//   5. 通过现有 Unix Domain Socket IPC 把指令下发给各子模块进程

#include "CarData.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using json = nlohmann::json;

// ─────────────────────────────────────────────
//  全局退出标志
// ─────────────────────────────────────────────
static std::atomic<bool> g_running{true};

// ─────────────────────────────────────────────
//  配置
// ─────────────────────────────────────────────
struct AiConfig {
    std::string api_key;
    std::string model    = "deepseek-v4-flash";
    std::string api_host = "api.deepseek.com";
    std::string api_path = "/chat/completions";
    int         api_port = 443;
};

// 解析 car_ai.conf，格式：key = value，# 开头为注释
static std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

static bool loadConfig(const std::string& path, AiConfig& cfg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[AI] 找不到配置文件: " << path << "\n";
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if      (key == "api_key") cfg.api_key  = val;
        else if (key == "model")   cfg.model    = val;
        else if (key == "api_url") {
            // 从完整 URL 里拆出 host / path / port
            // 支持格式：https://api.deepseek.com/chat/completions
            std::string url = val;
            if (url.substr(0, 8) == "https://") url = url.substr(8);
            else if (url.substr(0, 7) == "http://")  { url = url.substr(7); cfg.api_port = 80; }
            const auto slash = url.find('/');
            if (slash != std::string::npos) {
                cfg.api_host = url.substr(0, slash);
                cfg.api_path = url.substr(slash);
            } else {
                cfg.api_host = url;
                cfg.api_path = "/chat/completions";
            }
        }
    }
    if (cfg.api_key.empty()) {
        std::cerr << "[AI] 配置文件缺少 api_key\n";
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────
//  IPC 工具（复用 main.cpp 里相同的逻辑）
// ─────────────────────────────────────────────
static bool ipc_sendAll(int fd, const void* buf, size_t size) {
    const char* p = static_cast<const char*>(buf);
    size_t done = 0;
    while (done < size) {
        ssize_t n = send(fd, p + done, size - done, 0);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

static bool ipc_recvAll(int fd, void* buf, size_t size) {
    char* p = static_cast<char*>(buf);
    size_t done = 0;
    while (done < size) {
        ssize_t n = recv(fd, p + done, size - done, 0);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

static bool ipcRequest(const char* sock_path, Car::Msg& req, Car::Msg& resp) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    bool ok = false;
    Car::msgHdrToNetwork(req);  Car::msgValToNetwork(req);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        if (ipc_sendAll(fd, &req, sizeof(req)) && ipc_recvAll(fd, &resp, sizeof(resp))) {
            Car::msgHdrFromNetwork(resp);
            if (Car::isValidMsg(resp)) {
                Car::msgValFromNetwork(resp);
                ok = true;
            }
        }
    }
    close(fd);
    return ok;
}

// 读取 status 模块的当前车速，供安全状态机使用
static float getCurrentSpeed() {
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::READ;
    req.mod_id   = Car::ModuleID::STATUS;
    req.item_id  = 1; // speed
    req.val_type = Car::ValType::F32;
    if (ipcRequest(Car::SOCK_STATUS, req, resp) && resp.result == 0)
        return resp.value.f32;
    return 0.0f; // 读取失败时保守返回 0，不拦截
}

// ─────────────────────────────────────────────
//  JSON 工具（基于 nlohmann/json）
// ─────────────────────────────────────────────

// 从原始响应里剥掉 ```json ... ``` 的 markdown 壳
static std::string stripMarkdownFence(const std::string& s) {
    // 找第一个 { 和最后一个 }，直接截取中间部分
    const auto first = s.find('{');
    const auto last  = s.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last < first)
        return s;
    return s.substr(first, last - first + 1);
}

// 解析 actions 数组，每条 action 包含 module / field / value
struct Action {
    std::string module; // "air" / "door" / "status"
    std::string field;  // 字段名，对应 item_id 表
    double      value;  // 统一用 double，执行时再转型
};

static std::vector<Action> parseActions(const json& j) {
    std::vector<Action> result;
    try {
        if (!j.contains("actions") || !j["actions"].is_array()) return result;
        for (const auto& obj : j["actions"]) {
            if (!obj.contains("module") || !obj.contains("field") || !obj.contains("value"))
                continue;
            Action act;
            act.module = obj["module"].get<std::string>();
            act.field  = obj["field"].get<std::string>();
            act.value  = obj["value"].get<double>();
            result.push_back(act);
        }
    } catch (...) {}
    return result;
}

// ─────────────────────────────────────────────
//  安全状态机
// ─────────────────────────────────────────────
constexpr float SAFE_SPEED_THRESHOLD = 5.0f; // km/h，低于此速度才允许操作车门

// 判断某条 action 在当前车速下是否安全
// 返回 false 表示拦截，同时填写拦截原因
static bool isSafeAction(const Action& act, float speed, std::string& reason) {
    // 禁止 AI 控制档位（由用户手动操作）
    if (act.module == "status" && act.field == "gear") {
        reason = "档位操作不允许由 AI 控制，请手动操作";
        return false;
    }

    // 高速行驶时禁止开车门 / 开后备箱
    if (act.module == "door" && speed > SAFE_SPEED_THRESHOLD) {
        if (act.field != "lock_status") { // 锁门在任何速度下都允许
            reason = "车速 " + std::to_string(static_cast<int>(speed)) +
                     " km/h，禁止开门操作";
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────
//  字段路由表：module + field → sock_path / mod_id / item_id / val_type
// ─────────────────────────────────────────────
struct FieldMeta {
    const char*    sock;
    Car::ModuleID  mod;
    uint8_t        item_id;
    Car::ValType   val_type;
};

// 返回 nullptr 表示字段不存在
static const FieldMeta* getFieldMeta(const std::string& module, const std::string& field) {
    // 用静态数组存路由表，避免动态内存分配
    struct Entry { const char* mod; const char* field; FieldMeta meta; };
    static const Entry table[] = {
        // door
        {"door", "front_left",       {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   1, Car::ValType::U8}},
        {"door", "front_right",      {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   2, Car::ValType::U8}},
        {"door", "back_left",        {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   3, Car::ValType::U8}},
        {"door", "back_right",       {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   4, Car::ValType::U8}},
        {"door", "trunk",            {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   5, Car::ValType::U8}},
        {"door", "lock_status",      {Car::SOCK_DOOR,   Car::ModuleID::DOOR,   6, Car::ValType::U8}},
        // status（AI 只允许操作手刹，档位已在安全检查里拦截）
        {"status", "hand_brake",     {Car::SOCK_STATUS, Car::ModuleID::STATUS, 8, Car::ValType::U8}},
        // air
        {"air", "ac_switch",         {Car::SOCK_AIR,    Car::ModuleID::AIR,    1, Car::ValType::U8}},
        {"air", "fan_speed",         {Car::SOCK_AIR,    Car::ModuleID::AIR,    2, Car::ValType::U8}},
        {"air", "temp_set",          {Car::SOCK_AIR,    Car::ModuleID::AIR,    3, Car::ValType::I32}},
        {"air", "inner_cycle",       {Car::SOCK_AIR,    Car::ModuleID::AIR,    4, Car::ValType::U8}},
    };
    for (const auto& e : table) {
        if (e.mod == module && e.field == field)
            return &e.meta;
    }
    return nullptr;
}

// 执行一条 action，向对应子模块发送 IPC 写入指令
static bool executeAction(const Action& act) {
    const FieldMeta* meta = getFieldMeta(act.module, act.field);
    if (!meta) {
        std::cout << "  [跳过] 未知字段: " << act.module << "." << act.field << "\n";
        return false;
    }

    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id   = meta->mod;
    req.item_id  = meta->item_id;
    req.val_type = meta->val_type;

    switch (meta->val_type) {
        case Car::ValType::U8:  req.value.u8  = static_cast<uint8_t>(act.value);  break;
        case Car::ValType::I32: req.value.i32 = static_cast<int32_t>(act.value);  break;
        case Car::ValType::F32: req.value.f32 = static_cast<float>(act.value);    break;
        default: break;
    }

    return ipcRequest(meta->sock, req, resp) && resp.result == 0;
}

// ─────────────────────────────────────────────
//  DeepSeek HTTP 客户端（纯 POSIX socket + TLS via openssl s_client 管道）
//
//  WSL 下直接用 POSIX socket 做 TLS 握手需要链接 OpenSSL，增加依赖。
//  这里用更轻量的方案：把 HTTPS 请求委托给系统的 openssl s_client，
//  通过 popen 管道发送请求拿到响应，零额外依赖。
// ─────────────────────────────────────────────

// 多轮对话历史条目
struct ChatMsg { std::string role; std::string content; };

// 构建发给 DeepSeek 的完整请求 JSON
static std::string buildRequestJson(const AiConfig& cfg,
                                    const std::vector<ChatMsg>& history,
                                    const std::string& user_input) {
    // System prompt：约束模型只返回结构化 JSON，并告知安全规则
    static const std::string SYSTEM_PROMPT =
        "你是一个车载智能控制助手。用户用自然语言描述需求，你分析意图后，"
        "严格按以下 JSON 格式返回，不要输出任何其他内容，不要加 markdown 代码块：\n"
        "{\"reply\": \"对用户说的话\", \"actions\": [{\"module\": \"模块名\", "
        "\"field\": \"字段名\", \"value\": 数值}, ...]}\n\n"
        "支持的模块和字段：\n"
        "- door: front_left, front_right, back_left, back_right, trunk, lock_status (0=关/解锁, 1=开/锁定)\n"
        "- air: ac_switch(0/1), fan_speed(0-7), temp_set(整数°C), inner_cycle(0=外循环,1=内循环)\n"
        "- status: hand_brake(0=放下,1=拉起)\n\n"
        "安全规则：\n"
        "1. 禁止操作 gear 字段，档位只能手动控制\n"
        "2. 车速超过 5km/h 时，禁止开车门（lock_status 除外）\n"
        "3. 如果用户的话不涉及任何车控操作，actions 返回空数组 []\n"
        "4. value 必须是数字，不能是字符串";

    json j;
    j["model"]  = cfg.model;
    j["stream"] = false;

    j["messages"].push_back({{"role", "system"}, {"content", SYSTEM_PROMPT}});
    for (const auto& m : history)
        j["messages"].push_back({{"role", m.role}, {"content", m.content}});
    j["messages"].push_back({{"role", "user"}, {"content", user_input}});

    return j.dump();
}

// 校验 host 只含合法字符，防止命令注入
static bool isValidHost(const std::string& host) {
    if (host.empty() || host.size() > 253) return false;
    for (char c : host) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':')
            return false;
    }
    return true;
}

// 通过 openssl s_client 发送 HTTPS 请求，返回响应 body
// openssl s_client 在绝大多数 Linux / WSL 环境下默认已安装
static std::string httpsPost(const AiConfig& cfg, const std::string& body) {
    if (!isValidHost(cfg.api_host)) {
        std::cerr << "[AI] 非法 API 主机名: " << cfg.api_host << "\n";
        return "";
    }
    // 构造完整的 HTTP/1.1 请求报文
    std::ostringstream req;
    req << "POST " << cfg.api_path << " HTTP/1.1\r\n"
        << "Host: " << cfg.api_host << "\r\n"
        << "Authorization: Bearer " << cfg.api_key << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    const std::string req_str = req.str();

    char tmp_name[] = "/tmp/car_req_XXXXXX";
    int fd = mkstemp(tmp_name);
    if (fd < 0) return "";
    [[maybe_unused]] ssize_t written = write(fd, req_str.data(), req_str.size());
    if (static_cast<size_t>(written) != req_str.size()) {
        close(fd);
        unlink(tmp_name);
        std::cerr << "[AI] 临时文件写入失败\n";
        return "";
    }
    close(fd);

    // 用 openssl s_client 建立 TLS 连接并收发数据
    // -quiet 抑制握手调试输出，-connect 指定服务器
    const std::string cmd = "openssl s_client -quiet -connect " +
                            cfg.api_host + ":" + std::to_string(cfg.api_port) +
                            " < " + tmp_name + " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        unlink(tmp_name);
        std::cerr << "[AI] 无法启动 openssl s_client，请确认系统已安装 openssl\n";
        return "";
    }

    // 读取响应，30 秒超时，防止 API 卡死阻塞整个 AI 进程
    int pipe_fd = fileno(pipe);
    std::string response;
    char buf[4096];
    constexpr int TIMEOUT_SEC = 30;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(TIMEOUT_SEC);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            std::cerr << "[AI] API 请求超时（" << TIMEOUT_SEC << "s）\n";
            break;
        }
        int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

        pollfd pfd{pipe_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, ms);
        if (ret < 0) break;
        if (ret == 0) {
            std::cerr << "[AI] API 请求超时（" << TIMEOUT_SEC << "s）\n";
            break;
        }
        if (!(pfd.revents & POLLIN)) break;

        ssize_t n = read(pipe_fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        response += buf;
    }

    pclose(pipe);
    unlink(tmp_name);

    // 从 HTTP 响应里剥离 header，只保留 body
    const auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) return response;
    return response.substr(header_end + 4);
}

// 从 API 返回的响应 JSON 里提取 message.content 字段
// DeepSeek/OpenAI 格式：{"choices":[{"message":{"content":"..."}}]}
static std::string extractContent(const std::string& resp_json) {
    try {
        return json::parse(resp_json).at("choices").at(0).at("message").at("content").get<std::string>();
    } catch (...) {
        return "";
    }
}

// ─────────────────────────────────────────────
//  主对话循环
// ─────────────────────────────────────────────
static void runChatLoop(const AiConfig& cfg) {
    constexpr int MAX_HISTORY = 20; // 最多保留 10 轮（每轮 user + assistant 各一条）
    std::vector<ChatMsg> history;

    std::cout << "\n╔════════════════════════════════════════╗\n"
              << "║     智能车载 AI 助手  (输入 exit 退出)     ║\n"
              << "╚══════════════════════════════════════════╝\n"
              << "模型: " << cfg.model << "\n\n";

    while (g_running) {
        std::cout << "你: ";
        std::string input;
        if (!std::getline(std::cin, input)) break; // EOF（Ctrl+D）
        input = trim(input);
        if (input.empty()) continue;
        if (input == "exit" || input == "quit" || input == "退出") break;

        std::cout << "[AI] 思考中...\n";

        // 1. 构建请求 JSON
        const std::string req_body = buildRequestJson(cfg, history, input);

        // 2. 发送 HTTPS 请求
        const std::string raw_resp = httpsPost(cfg, req_body);
        if (raw_resp.empty()) {
            std::cout << "[AI] 请求失败，请检查网络或 API Key\n\n";
            continue;
        }

        // 3. 提取 message.content（模型输出的 JSON 字符串）
        const std::string content = extractContent(raw_resp);
        if (content.empty()) {
            std::cout << "[AI] 响应解析失败，原始响应:\n" << raw_resp.substr(0, 300) << "\n\n";
            continue;
        }

        // 4. 剥掉可能存在的 markdown 代码块壳
        const std::string clean = stripMarkdownFence(content);

        // 5. 解析 AI 返回的结构化 JSON（只 parse 一次，reply + actions 共享）
        json ai_json;
        try {
            ai_json = json::parse(clean);
        } catch (...) {
            std::cout << "\nAI: " << content << "\n(本次无车控指令)\n\n";
            continue;
        }

        const std::string reply = ai_json.value("reply", "");
        std::cout << "\nAI: " << (reply.empty() ? content : reply) << "\n";

        // 6. 解析 actions 数组
        const std::vector<Action> actions = parseActions(ai_json);

        if (actions.empty()) {
            std::cout << "(本次无车控指令)\n\n";
        } else {
            // 7. 安全状态机：查询当前车速
            const float speed = getCurrentSpeed();
            std::cout << "\n[执行指令] 当前车速: " << speed << " km/h\n";

            for (const auto& act : actions) {
                std::string reason;
                if (!isSafeAction(act, speed, reason)) {
                    // 安全检查不通过，拦截
                    std::cout << "  [拦截] " << act.module << "." << act.field
                              << " = " << act.value << "  原因: " << reason << "\n";
                    continue;
                }

                // 8. 通过 IPC 下发指令
                const bool ok = executeAction(act);
                std::cout << "  [" << (ok ? "成功" : "失败") << "] "
                          << act.module << "." << act.field
                          << " = " << act.value << "\n";
            }
            std::cout << "\n";
        }

        // 9. 把本轮对话追加进历史（assistant 存 reply 文本，不存原始 JSON）
        history.push_back({"user",      input});
        history.push_back({"assistant", reply.empty() ? content : reply});

        // 10. 超出上限时，丢掉最早的两条（一问一答）
        while (static_cast<int>(history.size()) > MAX_HISTORY)
            history.erase(history.begin(), history.begin() + 2);
    }

    std::cout << "\n[AI] 再见！\n";
}

// ─────────────────────────────────────────────
//  入口
// ─────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // 信号处理
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_running = false; };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // 配置文件路径（默认同目录，也可以命令行指定）
    std::string conf_path = "./car_ai.conf";
    if (argc >= 2) conf_path = argv[1];

    AiConfig cfg;
    if (!loadConfig(conf_path, cfg)) return 1;

    try {
        runChatLoop(cfg);
    } catch (const std::exception& e) {
        std::cerr << "[AI] 异常退出: " << e.what() << "\n";
        return 1;
    }
    return 0;
}