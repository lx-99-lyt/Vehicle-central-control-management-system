#include <gtest/gtest.h>
#include "CarData.hpp"
#include <cstring>

// ========== findField 测试 ==========

TEST(FindField, ExistingDoorField) {
    const auto* f = Car::findField("door", "front_left");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->item_id, 1);
    EXPECT_EQ(f->val_type, Car::ValType::U8);
    EXPECT_EQ(f->mod_id, Car::ModuleID::DOOR);
    EXPECT_TRUE(f->ai_accessible);
}

TEST(FindField, ExistingStatusField) {
    const auto* f = Car::findField("status", "speed");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->val_type, Car::ValType::F32);
    EXPECT_EQ(f->mod_id, Car::ModuleID::STATUS);
    EXPECT_FALSE(f->ai_accessible);
}

TEST(FindField, ExistingAirField) {
    const auto* f = Car::findField("air", "temp_set");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->val_type, Car::ValType::I32);
    EXPECT_EQ(f->mod_id, Car::ModuleID::AIR);
    EXPECT_TRUE(f->ai_accessible);
}

TEST(FindField, ExistingFaultField) {
    const auto* f = Car::findField("fault", "fault_codes");
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->val_type, Car::ValType::STR_U16);
    EXPECT_EQ(f->mod_id, Car::ModuleID::FAULT);
    EXPECT_FALSE(f->ai_accessible);
}

TEST(FindField, NonExistentModule) {
    EXPECT_EQ(Car::findField("engine", "rpm"), nullptr);
}

TEST(FindField, NonExistentField) {
    EXPECT_EQ(Car::findField("door", "nonexistent"), nullptr);
}

TEST(FindField, EmptyStrings) {
    EXPECT_EQ(Car::findField("", ""), nullptr);
    EXPECT_EQ(Car::findField("door", ""), nullptr);
    EXPECT_EQ(Car::findField("", "front_left"), nullptr);
}

TEST(FindField, AllDoorFieldsExist) {
    EXPECT_NE(Car::findField("door", "front_left"), nullptr);
    EXPECT_NE(Car::findField("door", "front_right"), nullptr);
    EXPECT_NE(Car::findField("door", "back_left"), nullptr);
    EXPECT_NE(Car::findField("door", "back_right"), nullptr);
    EXPECT_NE(Car::findField("door", "trunk"), nullptr);
    EXPECT_NE(Car::findField("door", "lock_status"), nullptr);
}

TEST(FindField, AllStatusFieldsExist) {
    EXPECT_NE(Car::findField("status", "speed"), nullptr);
    EXPECT_NE(Car::findField("status", "rpm"), nullptr);
    EXPECT_NE(Car::findField("status", "water_temp"), nullptr);
    EXPECT_NE(Car::findField("status", "oil_temp"), nullptr);
    EXPECT_NE(Car::findField("status", "fuel"), nullptr);
    EXPECT_NE(Car::findField("status", "battery_voltage"), nullptr);
    EXPECT_NE(Car::findField("status", "gear"), nullptr);
    EXPECT_NE(Car::findField("status", "hand_brake"), nullptr);
}

TEST(FindField, AllAirFieldsExist) {
    EXPECT_NE(Car::findField("air", "ac_switch"), nullptr);
    EXPECT_NE(Car::findField("air", "fan_speed"), nullptr);
    EXPECT_NE(Car::findField("air", "temp_set"), nullptr);
    EXPECT_NE(Car::findField("air", "inner_cycle"), nullptr);
}

TEST(FindField, AllFaultFieldsExist) {
    EXPECT_NE(Car::findField("fault", "fault_count"), nullptr);
    EXPECT_NE(Car::findField("fault", "fault_codes"), nullptr);
    EXPECT_NE(Car::findField("fault", "wring_light"), nullptr);
}

// ========== 网络字节序转换测试 ==========

TEST(MsgNetwork, HeaderToFromNetwork) {
    Car::Msg msg{};
    msg.magic = Car::CAR_MSG_MAGIC;
    msg.result = 42;

    Car::msgHdrToNetwork(msg);
    // 转换后 magic 和 result 应该是网络字节序
    EXPECT_NE(msg.magic, Car::CAR_MSG_MAGIC); // 在小端机器上会不同

    Car::msgHdrFromNetwork(msg);
    EXPECT_EQ(msg.magic, Car::CAR_MSG_MAGIC);
    EXPECT_EQ(msg.result, 42);
}

TEST(MsgNetwork, HeaderRoundtripZero) {
    Car::Msg msg{};
    msg.result = 0;
    Car::msgHdrToNetwork(msg);
    Car::msgHdrFromNetwork(msg);
    EXPECT_EQ(msg.result, 0);
}

TEST(MsgNetwork, HeaderRoundtripNegative) {
    Car::Msg msg{};
    msg.result = -1;
    Car::msgHdrToNetwork(msg);
    Car::msgHdrFromNetwork(msg);
    EXPECT_EQ(msg.result, -1);
}

TEST(MsgNetwork, ValI32Roundtrip) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::I32;
    msg.value.i32 = 12345;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_EQ(msg.value.i32, 12345);
}

TEST(MsgNetwork, ValI32Negative) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::I32;
    msg.value.i32 = -999;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_EQ(msg.value.i32, -999);
}

TEST(MsgNetwork, ValF32Roundtrip) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::F32;
    msg.value.f32 = 3.14f;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_FLOAT_EQ(msg.value.f32, 3.14f);
}

TEST(MsgNetwork, ValF32Zero) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::F32;
    msg.value.f32 = 0.0f;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_FLOAT_EQ(msg.value.f32, 0.0f);
}

TEST(MsgNetwork, ValF32Negative) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::F32;
    msg.value.f32 = -273.15f;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_FLOAT_EQ(msg.value.f32, -273.15f);
}

TEST(MsgNetwork, ValStrU16Roundtrip) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::STR_U16;
    msg.value.arr_u16[0] = 100;
    msg.value.arr_u16[1] = 200;
    msg.value.arr_u16[2] = 65535;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_EQ(msg.value.arr_u16[0], 100);
    EXPECT_EQ(msg.value.arr_u16[1], 200);
    EXPECT_EQ(msg.value.arr_u16[2], 65535);
}

TEST(MsgNetwork, ValStrI32Roundtrip) {
    Car::Msg msg{};
    msg.val_type = Car::ValType::STR_I32;
    msg.value.arr_i32[0] = 1000;
    msg.value.arr_i32[1] = -2000;
    msg.value.arr_i32[2] = 0;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_EQ(msg.value.arr_i32[0], 1000);
    EXPECT_EQ(msg.value.arr_i32[1], -2000);
    EXPECT_EQ(msg.value.arr_i32[2], 0);
}

TEST(MsgNetwork, ValU8NoConversion) {
    // U8 类型不需要字节序转换
    Car::Msg msg{};
    msg.val_type = Car::ValType::U8;
    msg.value.u8 = 42;
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_EQ(msg.value.u8, 42);
}

TEST(MsgNetwork, ValStrNoConversion) {
    // STR 类型不需要字节序转换
    Car::Msg msg{};
    msg.val_type = Car::ValType::STR;
    strncpy(msg.value.str, "hello", Car::MSG_STR_SIZE);
    Car::msgValToNetwork(msg);
    Car::msgValFromNetwork(msg);
    EXPECT_STREQ(msg.value.str, "hello");
}

// ========== FIELD_TABLE 完整性测试 ==========

TEST(FieldTable, NoDuplicateEntries) {
    for (size_t i = 0; i < std::size(Car::FIELD_TABLE); ++i) {
        for (size_t j = i + 1; j < std::size(Car::FIELD_TABLE); ++j) {
            bool same_module = std::string_view(Car::FIELD_TABLE[i].module) ==
                               std::string_view(Car::FIELD_TABLE[j].module);
            bool same_field = std::string_view(Car::FIELD_TABLE[i].field) ==
                              std::string_view(Car::FIELD_TABLE[j].field);
            EXPECT_FALSE(same_module && same_field)
                << "Duplicate: " << Car::FIELD_TABLE[i].module << "." << Car::FIELD_TABLE[i].field;
        }
    }
}

TEST(FieldTable, AllHaveValidSockPath) {
    for (const auto& f : Car::FIELD_TABLE) {
        EXPECT_NE(f.sock_path, nullptr);
        EXPECT_GT(strlen(f.sock_path), 0);
    }
}

// ========== Msg 默认值测试 ==========

TEST(MsgDefaults, MagicAndVersion) {
    Car::Msg msg{};
    EXPECT_EQ(msg.magic, Car::CAR_MSG_MAGIC);
    EXPECT_EQ(msg.version, Car::CAR_MSG_VERSION);
    EXPECT_EQ(msg.reserved, 0);
}

TEST(MsgDefaults, ZeroInitialized) {
    Car::Msg msg{};
    EXPECT_EQ(msg.item_id, 0);
    EXPECT_EQ(msg.result, 0);
    EXPECT_EQ(msg.value.u8, 0);
}

// ========== 枚举值测试 ==========

TEST(EnumValues, ModuleID) {
    EXPECT_EQ(static_cast<uint8_t>(Car::ModuleID::DOOR), 1);
    EXPECT_EQ(static_cast<uint8_t>(Car::ModuleID::STATUS), 2);
    EXPECT_EQ(static_cast<uint8_t>(Car::ModuleID::AIR), 3);
    EXPECT_EQ(static_cast<uint8_t>(Car::ModuleID::FAULT), 4);
}

TEST(EnumValues, MsgType) {
    EXPECT_EQ(static_cast<uint8_t>(Car::MsgType::CMD), 1);
    EXPECT_EQ(static_cast<uint8_t>(Car::MsgType::RESPONSE), 2);
}

TEST(EnumValues, CmdType) {
    EXPECT_EQ(static_cast<uint8_t>(Car::CmdType::READ), 1);
    EXPECT_EQ(static_cast<uint8_t>(Car::CmdType::WRITE), 2);
    EXPECT_EQ(static_cast<uint8_t>(Car::CmdType::GET_ALL), 3);
}
