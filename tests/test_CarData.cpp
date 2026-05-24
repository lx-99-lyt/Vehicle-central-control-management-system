#include <gtest/gtest.h>
#include "CarData.hpp"

TEST(GearToText, AllGears) {
    EXPECT_STREQ(Car::gearToText(static_cast<uint8_t>(Car::Gear::P)), "P");
    EXPECT_STREQ(Car::gearToText(static_cast<uint8_t>(Car::Gear::R)), "R");
    EXPECT_STREQ(Car::gearToText(static_cast<uint8_t>(Car::Gear::N)), "N");
    EXPECT_STREQ(Car::gearToText(static_cast<uint8_t>(Car::Gear::D)), "D");
    EXPECT_STREQ(Car::gearToText(99), "UNKNOWN");
}

TEST(IsValidMsg, ValidMessage) {
    Car::Msg msg{};
    EXPECT_TRUE(Car::isValidMsg(msg));
}

TEST(IsValidMsg, BadMagic) {
    Car::Msg msg{};
    msg.magic = 0xDEAD;
    EXPECT_FALSE(Car::isValidMsg(msg));
}

TEST(IsValidMsg, BadVersion) {
    Car::Msg msg{};
    msg.version = 99;
    EXPECT_FALSE(Car::isValidMsg(msg));
}

TEST(MsgLayout, ValueUnionSize) {
    // value union 必须能容纳 MSG_STR_SIZE 字节的字符串
    EXPECT_GE(sizeof(Car::Msg::value), Car::MSG_STR_SIZE);
    // value union 大小由最大成员 arr_i32[MSG_ARR_SIZE] 决定
    EXPECT_EQ(sizeof(Car::Msg::value), Car::MSG_ARR_SIZE * sizeof(int32_t));
}

TEST(MsgLayout, HeaderOffsets) {
    // 验证 #pragma pack(1) 生效：头部字段间无填充
    EXPECT_EQ(offsetof(Car::Msg, version),  sizeof(uint16_t));
    EXPECT_EQ(offsetof(Car::Msg, value),    sizeof(uint16_t) + sizeof(uint8_t) * 7);
    EXPECT_EQ(offsetof(Car::Msg, result),   sizeof(uint16_t) + sizeof(uint8_t) * 7 + sizeof(Car::Msg::value));
}

TEST(StateStructs, FitInMsgUnion) {
    EXPECT_LE(sizeof(Car::DoorState),   sizeof(Car::Msg::value));
    EXPECT_LE(sizeof(Car::StatusState), sizeof(Car::Msg::value));
    EXPECT_LE(sizeof(Car::AirState),    sizeof(Car::Msg::value));
    EXPECT_LE(sizeof(Car::FaultState),  sizeof(Car::Msg::value));
}

TEST(MagicVersion, Constants) {
    EXPECT_EQ(Car::CAR_MSG_MAGIC, 0xCA12u);
    EXPECT_EQ(Car::CAR_MSG_VERSION, 1);
}
