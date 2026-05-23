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

TEST(MsgLayout, Sizes) {
    // magic(2) + version(1) + reserved(1) + mod_id(1) + msg_type(1)
    // + cmd_type(1) + item_id(1) + val_type(1) + value(64) + result(4)
    EXPECT_EQ(sizeof(Car::Msg), 141);
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
