#include <gtest/gtest.h>
#include "StatusModule.hpp"
#include <cstring>

class StatusModuleTest : public ::testing::Test {
protected:
    class TestableStatusModule : public StatusModule {
    public:
        void testProcessCommand(const Car::Msg& request, Car::Msg& response) {
            processCommand(request, response);
        }
    };

    TestableStatusModule module;
    Car::Msg req{};
    Car::Msg resp{};

    void SetUp() override {
        req.magic = Car::CAR_MSG_MAGIC;
        req.version = Car::CAR_MSG_VERSION;
        req.mod_id = Car::ModuleID::STATUS;
        req.msg_type = Car::MsgType::CMD;
    }
};

// ========== GET_ALL 测试 ==========

TEST_F(StatusModuleTest, GetAllReturnsAllFields) {
    req.cmd_type = Car::CmdType::GET_ALL;
    module.testProcessCommand(req, resp);

    EXPECT_EQ(resp.mod_id, Car::ModuleID::STATUS);
    EXPECT_EQ(resp.result, 0);
    EXPECT_EQ(resp.val_type, Car::ValType::STR_U8);
}

// ========== WRITE + READ 联合测试 ==========

TEST_F(StatusModuleTest, WriteReadSpeed) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.f32 = 120.5f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::F32);
    EXPECT_FLOAT_EQ(readResp.value.f32, 120.5f);
}

TEST_F(StatusModuleTest, WriteReadRpm) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    req.value.i32 = 3500;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::I32);
    EXPECT_EQ(readResp.value.i32, 3500);
}

TEST_F(StatusModuleTest, WriteReadWaterTemp) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.f32 = 90.5f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::F32);
    EXPECT_FLOAT_EQ(readResp.value.f32, 90.5f);
}

TEST_F(StatusModuleTest, WriteReadOilTemp) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 4;
    req.value.f32 = 85.0f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 4;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::F32);
    EXPECT_FLOAT_EQ(readResp.value.f32, 85.0f);
}

TEST_F(StatusModuleTest, WriteReadFuel) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 5;
    req.value.f32 = 75.0f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 5;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::F32);
    EXPECT_FLOAT_EQ(readResp.value.f32, 75.0f);
}

TEST_F(StatusModuleTest, WriteReadBatteryVoltage) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 6;
    req.value.f32 = 12.8f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 6;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::F32);
    EXPECT_FLOAT_EQ(readResp.value.f32, 12.8f);
}

TEST_F(StatusModuleTest, WriteReadGear) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 7;
    req.value.u8 = static_cast<uint8_t>(Car::Gear::D);
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 7;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, static_cast<uint8_t>(Car::Gear::D));
}

TEST_F(StatusModuleTest, WriteReadHandBrake) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 8;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 8;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 1);
}

// ========== READ 初始值测试 ==========

TEST_F(StatusModuleTest, ReadInitialStateIsZero) {
    for (uint8_t itemId = 1; itemId <= 8; ++itemId) {
        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = itemId;
        module.testProcessCommand(readReq, readResp);
        if (itemId == 1 || itemId == 3 || itemId == 4 || itemId == 5 || itemId == 6) {
            EXPECT_FLOAT_EQ(readResp.value.f32, 0.0f) << "item_id=" << static_cast<int>(itemId);
        } else {
            EXPECT_EQ(readResp.value.u8, 0) << "item_id=" << static_cast<int>(itemId);
        }
    }
}

// ========== 边界值测试 ==========

TEST_F(StatusModuleTest, SpeedZero) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.f32 = 0.0f;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_FLOAT_EQ(readResp.value.f32, 0.0f);
}

TEST_F(StatusModuleTest, RpmNegative) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    req.value.i32 = -1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.i32, -1);
}

TEST_F(StatusModuleTest, AllGearsWritable) {
    for (uint8_t g = 0; g <= 3; ++g) {
        req.cmd_type = Car::CmdType::WRITE;
        req.item_id = 7;
        req.value.u8 = g;
        module.testProcessCommand(req, resp);

        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = 7;
        module.testProcessCommand(readReq, readResp);
        EXPECT_EQ(readResp.value.u8, g);
    }
}

// ========== 响应元数据 ==========

TEST_F(StatusModuleTest, ResponseHasCorrectModuleId) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.mod_id, Car::ModuleID::STATUS);
}

TEST_F(StatusModuleTest, ResponseResultIsZero) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.result, 0);
}
