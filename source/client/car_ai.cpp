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
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
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
    bool        use_https = true;
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
            std::string url = val;
            if (url.substr(0, 8) == "https://") { url = url.substr(8); cfg.use_https = true; }
            else if (url.substr(0, 7) == "http://") { url = url.substr(7); cfg.use_https = false; cfg.api_port = 80; }
            const auto slash = url.find('/');
            std::string host_port;
            if (slash != std::string::npos) {
                host_port = url.substr(0, slash);
                cfg.api_path = url.substr(slash);
            } else {
                host_port = url;
                cfg.api_path = "/chat/completions";
            }
            // 提取 host:port
            const auto colon = host_port.find(':');
            if (colon != std::string::npos) {
                cfg.api_host = host_port.substr(0, colon);
                cfg.api_port = std::stoi(host_port.substr(colon + 1));
            } else {
                cfg.api_host = host_port;
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

// 清洗非法 UTF-8 字节，替换为 '?'
static std::string sanitizeUtf8(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        size_t len = 0;
        if (c < 0x80)       len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else { result += '?'; ++i; continue; }

        if (i + len > s.size()) { result += '?'; break; }

        bool valid = true;
        for (size_t j = 1; j < len; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) { valid = false; break; }
        }
        if (valid) { result.append(s, i, len); i += len; }
        else { result += '?'; ++i; }
    }
    return result;
}

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

// 执行一条 action，向对应子模块发送 IPC 写入指令
static bool executeAction(const Action& act) {
    const Car::FieldMeta* meta = Car::findField(act.module, act.field);
    if (!meta) {
        std::cout << "  [跳过] 未知字段: " << act.module << "." << act.field << "\n";
        return false;
    }
    if (!meta->ai_accessible) {
        std::cout << "  [跳过] AI 无权操作: " << act.module << "." << act.field << "\n";
        return false;
    }

    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id   = meta->mod_id;
    req.item_id  = meta->item_id;
    req.val_type = meta->val_type;

    switch (meta->val_type) {
        case Car::ValType::U8:  req.value.u8  = static_cast<uint8_t>(act.value);  break;
        case Car::ValType::I32: req.value.i32 = static_cast<int32_t>(act.value);  break;
        case Car::ValType::F32: req.value.f32 = static_cast<float>(act.value);    break;
        default: break;
    }

    return ipcRequest(meta->sock_path, req, resp) && resp.result == 0;
}

// ─────────────────────────────────────────────
//  DeepSeek HTTP 客户端（libcurl）
// ─────────────────────────────────────────────

#include <curl/curl.h>

// 多轮对话历史条目
struct ChatMsg { std::string role; std::string content; };

// 构建发给 DeepSeek 的完整请求 JSON
static std::string buildRequestJson(const AiConfig& cfg,
                                    const std::vector<ChatMsg>& history,
                                    const std::string& user_input) {
    // System prompt：约束模型只返回结构化 JSON
    static const std::string SYSTEM_PROMPT =
        "你是车载控制助手。必须且只能返回如下JSON，禁止返回任何其他文字：\n"
        "{\"reply\": \"回复内容\", \"actions\": [{\"module\": \"模块\", \"field\": \"字段\", \"value\": 数值}]}\n\n"
        "模块和字段：\n"
        "- air: ac_switch(0/1), fan_speed(0-7), temp_set(整数°C), inner_cycle(0/1)\n"
        "- door: front_left/front_right/back_left/back_right/trunk(0/1), lock_status(0/1)\n"
        "- status: hand_brake(0/1)\n\n"
        "严格规则：\n"
        "1. 只要用户的话涉及车控意图（开/关/调/锁/热/冷等），actions必须包含对应指令，不能只在reply里说做了但actions为空\n"
        "2. 只有用户只是闲聊（如\"今天天气怎么样\"）时，actions才返回空数组\n"
        "3. value必须是数字\n\n"
        "例子：\n"
        "用户\"打开空调调到26度\"→ {\"reply\":\"已开启空调设置26°C\", \"actions\":[{\"module\":\"air\",\"field\":\"ac_switch\",\"value\":1},{\"module\":\"air\",\"field\":\"temp_set\",\"value\":26}]}\n"
        "用户\"有点冷\"→ {\"reply\":\"已调高到24°C\", \"actions\":[{\"module\":\"air\",\"field\":\"temp_set\",\"value\":24}]}\n"
        "用户\"有点热\"→ {\"reply\":\"已调低到22°C\", \"actions\":[{\"module\":\"air\",\"field\":\"temp_set\",\"value\":22}]}\n"
        "用户\"还是热\"→ {\"reply\":\"已调低到20°C\", \"actions\":[{\"module\":\"air\",\"field\":\"temp_set\",\"value\":20}]}\n"
        "用户\"锁车门\"→ {\"reply\":\"已锁门\", \"actions\":[{\"module\":\"door\",\"field\":\"lock_status\",\"value\":1}]}\n"
        "用户\"空调调到33度\"→ {\"reply\":\"已设置33°C\", \"actions\":[{\"module\":\"air\",\"field\":\"temp_set\",\"value\":33}]}\n"
        "用户\"关掉空调\"→ {\"reply\":\"已关闭空调\", \"actions\":[{\"module\":\"air\",\"field\":\"ac_switch\",\"value\":0}]}";

    json j;
    j["model"]  = cfg.model;
    j["stream"] = false;

    j["messages"].push_back(json{{"role", "system"}, {"content", SYSTEM_PROMPT}});
    for (const auto& m : history)
        j["messages"].push_back(json{{"role", m.role}, {"content", m.content}});
    j["messages"].push_back(json{{"role", "user"}, {"content", user_input}});

    return j.dump();
}

// libcurl 写回调：将响应数据追加到 string
static size_t curlWriteCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    const size_t total = size * nmemb;
    buf->append(static_cast<const char*>(ptr), total);
    return total;
}

// 通过 libcurl 发送 HTTP/HTTPS 请求，返回响应 body
static std::string httpsPost(const AiConfig& cfg, const std::string& body) {
    const std::string scheme = cfg.use_https ? "https" : "http";
    const bool show_port = cfg.use_https ? (cfg.api_port != 443) : (cfg.api_port != 80);
    const std::string url = scheme + "://" + cfg.api_host +
                            (show_port ? ":" + std::to_string(cfg.api_port) : "") +
                            cfg.api_path;

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[AI] curl_easy_init 失败\n";
        return "";
    }

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "CarAI/1.0");

    const CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        std::cerr << "[AI] curl 请求失败: " << curl_easy_strerror(res) << "\n";

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK) ? response : "";
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

        // 4. 清洗非法 UTF-8 + 剥掉 markdown 代码块壳
        const std::string clean = stripMarkdownFence(sanitizeUtf8(content));

        // 5. 解析 AI 返回的结构化 JSON
        json ai_json;
        try {
            ai_json = json::parse(clean);
        } catch (...) {
            std::cout << "\nAI: " << content << "\n(本次无车控指令)\n\n";
            continue;
        }

        std::string reply;
        try {
            reply = ai_json.value("reply", "");
        } catch (...) {
            reply = content;
        }
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