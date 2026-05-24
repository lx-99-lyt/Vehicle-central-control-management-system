#pragma once
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// 用namespace隔离代码，避免命名冲突
namespace Car{

// 定义socket路径
constexpr const char* SOCK_DOOR = "/tmp/car_door.sock";
constexpr const char* SOCK_STATUS = "/tmp/car_status.sock";
constexpr const char* SOCK_AIR = "/tmp/car_air.sock";
constexpr const char* SOCK_FAULT  = "/tmp/car_fault.sock";

constexpr const char* LOG_FILE_PATH = "./car_log.txt";
constexpr const char* INI_FILE_PATH = "./car_info.ini";
constexpr int MAX_FAULT_CODE = 10;
constexpr int DEVICE_NAME_LEN = 32;
constexpr size_t MSG_STR_SIZE = 64;
constexpr size_t MSG_ARR_SIZE = 32;
constexpr uint16_t CAR_MSG_MAGIC = 0xCA12;
constexpr uint8_t  CAR_MSG_VERSION = 1;

enum class ModuleID : uint8_t { DOOR = 1, STATUS, AIR, FAULT };
enum class MsgType : uint8_t { CMD = 1, RESPONSE = 2 };
enum class CmdType : uint8_t { READ = 1, WRITE = 2, GET_ALL = 3 };
enum class ValType : uint8_t { U8, I32, F32, STR, STR_U8, STR_U16, STR_I32, STR_F32} ;
enum class Gear : uint8_t { P = 0, R = 1, N = 2, D = 3 };

// 设置内存对齐为1字节，确保结构体在网络传输时没有填充字节
#pragma pack(push, 1)
struct DoorState { uint8_t front_left, front_right, back_left, back_right, trunk, lock_status; };
struct StatusState { float speed; int rpm; float water_temp, oil_temp, fuel, battery_voltage; uint8_t gear, hand_brake; };
struct AirState { uint8_t ac_switch, fan_speed; int temp_set; uint8_t inner_cycle; };
struct FaultState { uint8_t fault_count; uint16_t fault_codes[MAX_FAULT_CODE]; uint8_t wring_light; };

// IPC消息结构体
struct Msg {
    uint16_t magic   = CAR_MSG_MAGIC;
    uint8_t  version = CAR_MSG_VERSION;
    uint8_t  reserved = 0;
    ModuleID mod_id;
    MsgType msg_type;
    CmdType cmd_type;
    uint8_t item_id;
    ValType val_type;
    union {
        uint8_t u8;
        int32_t i32;
        float f32;
        char str[MSG_STR_SIZE];
        uint8_t arr_u8[MSG_ARR_SIZE];
        uint16_t arr_u16[MSG_ARR_SIZE];
        int32_t arr_i32[MSG_ARR_SIZE];
    } value;
    int result; // 用于响应消息，表示操作结果
};

#pragma pack(pop) // 恢复默认内存对齐

[[nodiscard]] constexpr bool isValidMsg(const Msg& msg) {
    return msg.magic == CAR_MSG_MAGIC && msg.version == CAR_MSG_VERSION;
}

// 编译期安全检查：确保各状态结构体能装进 Msg.value union
// 若未来给任意结构体加字段导致超出，编译器会直接报错，绝不会运行时踩内存
static_assert(sizeof(DoorState)   <= sizeof(Msg::value), "DoorState too large for Msg union");
static_assert(sizeof(StatusState) <= sizeof(Msg::value), "StatusState too large for Msg union");
static_assert(sizeof(AirState)    <= sizeof(Msg::value), "AirState too large for Msg union");
static_assert(sizeof(FaultState)  <= sizeof(Msg::value), "FaultState too large for Msg union");

// ─────────────────────────────────────────────
//  统一字段元数据表 —— 全系统唯一字段定义来源
//  car_ctl / car_ai / 各子模块都引用此表，新增字段只需改这一处
// ─────────────────────────────────────────────
struct FieldMeta {
    const char* module;
    const char* field;
    const char* sock_path;
    uint8_t     item_id;
    ValType     val_type;
    ModuleID    mod_id;
    bool        ai_accessible;  // AI 是否可控制此字段
};

inline constexpr FieldMeta FIELD_TABLE[] = {
    // fields: module, field, sock_path, item_id, val_type, mod_id, ai_accessible
    // -- door --
    {"door", "front_left",   SOCK_DOOR,   1, ValType::U8,  ModuleID::DOOR,   true},
    {"door", "front_right",  SOCK_DOOR,   2, ValType::U8,  ModuleID::DOOR,   true},
    {"door", "back_left",    SOCK_DOOR,   3, ValType::U8,  ModuleID::DOOR,   true},
    {"door", "back_right",   SOCK_DOOR,   4, ValType::U8,  ModuleID::DOOR,   true},
    {"door", "trunk",        SOCK_DOOR,   5, ValType::U8,  ModuleID::DOOR,   true},
    {"door", "lock_status",  SOCK_DOOR,   6, ValType::U8,  ModuleID::DOOR,   true},
    // -- status --
    {"status", "speed",           SOCK_STATUS, 1, ValType::F32, ModuleID::STATUS, false},
    {"status", "rpm",             SOCK_STATUS, 2, ValType::I32, ModuleID::STATUS, false},
    {"status", "water_temp",      SOCK_STATUS, 3, ValType::F32, ModuleID::STATUS, false},
    {"status", "oil_temp",        SOCK_STATUS, 4, ValType::F32, ModuleID::STATUS, false},
    {"status", "fuel",            SOCK_STATUS, 5, ValType::F32, ModuleID::STATUS, false},
    {"status", "battery_voltage", SOCK_STATUS, 6, ValType::F32, ModuleID::STATUS, false},
    {"status", "gear",            SOCK_STATUS, 7, ValType::U8,  ModuleID::STATUS, false},
    {"status", "hand_brake",      SOCK_STATUS, 8, ValType::U8,  ModuleID::STATUS, true},
    // -- air --
    {"air", "ac_switch",    SOCK_AIR, 1, ValType::U8,  ModuleID::AIR, true},
    {"air", "fan_speed",    SOCK_AIR, 2, ValType::U8,  ModuleID::AIR, true},
    {"air", "temp_set",     SOCK_AIR, 3, ValType::I32, ModuleID::AIR, true},
    {"air", "inner_cycle",  SOCK_AIR, 4, ValType::U8,  ModuleID::AIR, true},
    // -- fault --
    {"fault", "fault_count", SOCK_FAULT, 1, ValType::U8,      ModuleID::FAULT, false},
    {"fault", "fault_codes", SOCK_FAULT, 2, ValType::STR_U16, ModuleID::FAULT, false},
    {"fault", "wring_light", SOCK_FAULT, 3, ValType::U8,      ModuleID::FAULT, false},
};

// 按模块名 + 字段名查找元数据，未找到返回 nullptr
inline const FieldMeta* findField(const std::string& module, const std::string& field) {
    for (const auto& m : FIELD_TABLE) {
        if (m.module == module && m.field == field)
            return &m;
    }
    return nullptr;
}

// 网络字节序转换：Unix Domain Socket 本地通信无需转换（收发同机同字节序），
// 但预留 hton/ntoh 接口，将来扩展 TCP 跨架构通信时只需解除注释即可工作。
inline void msgHdrToNetwork(Msg& msg) {
    msg.magic  = htons(msg.magic);
    msg.result = htonl(msg.result);
}
inline void msgHdrFromNetwork(Msg& msg) {
    msg.magic  = ntohs(msg.magic);
    msg.result = ntohl(msg.result);
}
inline void msgValToNetwork(Msg& msg) {
    switch (msg.val_type) {
        case ValType::I32:     msg.value.i32 = htonl(msg.value.i32); break;
        case ValType::F32: {   uint32_t t = 0; std::memcpy(&t, &msg.value.f32, 4); t = htonl(t); std::memcpy(&msg.value.f32, &t, 4); break; }
        case ValType::STR_U16: for (auto& v : msg.value.arr_u16) v = htons(v); break;
        case ValType::STR_I32: for (auto& v : msg.value.arr_i32) v = htonl(v); break;
        default: break;
    }
}
inline void msgValFromNetwork(Msg& msg) {
    switch (msg.val_type) {
        case ValType::I32:     msg.value.i32 = ntohl(msg.value.i32); break;
        case ValType::F32: {   uint32_t t = 0; std::memcpy(&t, &msg.value.f32, 4); t = ntohl(t); std::memcpy(&msg.value.f32, &t, 4); break; }
        case ValType::STR_U16: for (auto& v : msg.value.arr_u16) v = ntohs(v); break;
        case ValType::STR_I32: for (auto& v : msg.value.arr_i32) v = ntohl(v); break;
        default: break;
    }
}

[[nodiscard]] constexpr const char* gearToText(uint8_t gear) {
    switch (static_cast<Gear>(gear)) {
        case Gear::P: return "P";
        case Gear::R: return "R";
        case Gear::N: return "N";
        case Gear::D: return "D";
        default: return "UNKNOWN";
    }
}

} // namespace Car