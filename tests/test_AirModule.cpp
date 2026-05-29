#include <gtest/gtest.h>
#include "AirModule.hpp"
#include <cstring>

class AirModuleTest : public ::testing::Test {
protected:
    class TestableAirModule : public AirModule {
    public:
        void testProcessCommand(const Car::Msg& request, Car::Msg& response) {
            processCommand(request, response);
        }
    };

    TestableAirModule module;
    Car::Msg req{};
    Car::Msg resp{};

    void SetUp() override {
        req.magic = Car::CAR_MSG_MAGIC;
        req.version = Car::CAR_MSG_VERSION;
        req.mod_id = Car::ModuleID::AIR;
        req.msg_type = Car::MsgType::CMD;
    }
};

// ========== GET_ALL 测试 ==========

TEST_F(AirModuleTest, GetAllReturnsAllFields) {
    req.cmd_type = Car::CmdType::GET_ALL;
    module.testProcessCommand(req, resp);

    EXPECT_EQ(resp.mod_id, Car::ModuleID::AIR);
    EXPECT_EQ(resp.result, 0);
    EXPECT_EQ(resp.val_type, Car::ValType::STR_U8);
}

// ========== WRITE + READ 联合测试 ==========

TEST_F(AirModuleTest, WriteReadAcSwitch) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(AirModuleTest, WriteReadFanSpeed) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    req.value.u8 = 5;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 5);
}

TEST_F(AirModuleTest, WriteReadTempSet) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.i32 = 26;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::I32);
    EXPECT_EQ(readResp.value.i32, 26);
}

TEST_F(AirModuleTest, WriteReadInnerCycle) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 4;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 4;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 1);
}

// ========== READ 初始值测试 ==========

TEST_F(AirModuleTest, ReadInitialStateIsZero) {
    for (uint8_t itemId = 1; itemId <= 4; ++itemId) {
        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = itemId;
        module.testProcessCommand(readReq, readResp);
        if (itemId == 3) {
            EXPECT_EQ(readResp.value.i32, 0) << "temp_set should be 0 initially";
        } else {
            EXPECT_EQ(readResp.value.u8, 0) << "item_id=" << static_cast<int>(itemId);
        }
    }
}

// ========== 温度边界值测试 ==========

TEST_F(AirModuleTest, TempSetNegative) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.i32 = -10;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.i32, -10);
}

TEST_F(AirModuleTest, TempSetHighValue) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.i32 = 99;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.i32, 99);
}

// ========== 风速边界值测试 ==========

TEST_F(AirModuleTest, FanSpeedMax) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    req.value.u8 = 7;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 7);
}

// ========== 响应元数据 ==========

TEST_F(AirModuleTest, ResponseHasCorrectModuleId) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.mod_id, Car::ModuleID::AIR);
}

TEST_F(AirModuleTest, ResponseResultIsZero) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.result, 0);
}
