#include "AsyncAuditLogger.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <vector>
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

    auto* conn = static_cast<MYSQL*>(m_mysql);

    if (mysql_ping(conn) != 0) {
        m_connected = false;
        return false;
    }

    // 使用 mysql_real_escape_string 防止 SQL 注入
    auto esc = [conn](const std::string& s) {
        std::vector<char> buf(s.size() * 2 + 1);
        mysql_real_escape_string(conn, buf.data(), s.c_str(), s.size());
        return std::string(buf.data());
    };

    std::string sql = "INSERT INTO audit_logs (event_type, action, speed, reason) VALUES ('";
    sql += esc(entry.event_type);
    sql += "', '";
    sql += esc(entry.action);
    sql += "', ";
    sql += std::to_string(entry.speed);
    sql += ", '";
    sql += esc(entry.reason);
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
        if (!m_connected) {
            if (reconnect()) {
                current_interval = m_reconnect_base_ms;
                std::cout << "[AsyncAuditLogger] MySQL reconnected\n";
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(current_interval));
                current_interval = std::min(current_interval * 2, m_reconnect_max_ms);
                continue;
            }
        }

        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || !m_running; });

        if (!m_running && m_queue.empty()) break;

        std::deque<AuditLogEntry> batch;
        batch.swap(m_queue);
        lock.unlock();

        for (size_t i = 0; i < batch.size(); ++i) {
            if (!insertLog(batch[i])) {
                std::lock_guard<std::mutex> qlock(m_queue_mutex);
                m_queue.insert(m_queue.begin(), batch.begin() + static_cast<long>(i), batch.end());
                break;
            }
        }
    }
}

void AsyncAuditLogger::init(const std::string& conf_path) {
    if (m_running) return;

    parseConfig(conf_path);
    connectMySQL();

    m_running = true;
    m_worker = std::thread(&AsyncAuditLogger::workerLoop, this);
}

void AsyncAuditLogger::enqueue(const std::string& event_type,
                                const std::string& action,
                                float speed,
                                const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_queue_mutex);

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
