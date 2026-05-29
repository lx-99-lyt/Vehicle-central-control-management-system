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
