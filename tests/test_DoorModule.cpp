#include <gtest/gtest.h>
#include "DoorModule.hpp"
#include <cstring>

class DoorModuleTest : public ::testing::Test {
protected:
    // 通过继承暴露 processCommand 以便测试
    class TestableDoorModule : public DoorModule {
    public:
        void testProcessCommand(const Car::Msg& request, Car::Msg& response) {
            processCommand(request, response);
        }
    };

    TestableDoorModule module;
    Car::Msg req{};
    Car::Msg resp{};

    void SetUp() override {
        req.magic = Car::CAR_MSG_MAGIC;
        req.version = Car::CAR_MSG_VERSION;
        req.mod_id = Car::ModuleID::DOOR;
        req.msg_type = Car::MsgType::CMD;
    }
};

// ========== GET_ALL 测试 ==========

TEST_F(DoorModuleTest, GetAllReturnsAllFields) {
    req.cmd_type = Car::CmdType::GET_ALL;
    module.testProcessCommand(req, resp);

    EXPECT_EQ(resp.mod_id, Car::ModuleID::DOOR);
    EXPECT_EQ(resp.result, 0);
    EXPECT_EQ(resp.val_type, Car::ValType::STR_U8);
}

// ========== WRITE 测试 ==========

TEST_F(DoorModuleTest, WriteFrontLeft) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    EXPECT_EQ(resp.result, 0);
    EXPECT_EQ(resp.value.u8, 1);

    // READ 验证
    Car::Msg readReq{}, readResp{};
    readReq.magic = Car::CAR_MSG_MAGIC;
    readReq.version = Car::CAR_MSG_VERSION;
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(DoorModuleTest, WriteFrontRight) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(DoorModuleTest, WriteBackLeft) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(DoorModuleTest, WriteBackRight) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 4;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 4;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(DoorModuleTest, WriteTrunk) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 5;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 5;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

TEST_F(DoorModuleTest, WriteLockStatus) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 6;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 6;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 1);
}

// ========== READ 测试 ==========

TEST_F(DoorModuleTest, ReadInitialStateIsZero) {
    // 新模块所有字段应为零
    for (uint8_t itemId = 1; itemId <= 6; ++itemId) {
        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = itemId;
        module.testProcessCommand(readReq, readResp);
        EXPECT_EQ(readResp.value.u8, 0) << "item_id=" << static_cast<int>(itemId);
    }
}

TEST_F(DoorModuleTest, ReadAllFieldsReturnU8) {
    for (uint8_t itemId = 1; itemId <= 6; ++itemId) {
        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = itemId;
        module.testProcessCommand(readReq, readResp);
        EXPECT_EQ(readResp.val_type, Car::ValType::U8) << "item_id=" << static_cast<int>(itemId);
    }
}

// ========== WRITE + READ 联合测试 ==========

TEST_F(DoorModuleTest, WriteThenReadAllFields) {
    for (uint8_t itemId = 1; itemId <= 6; ++itemId) {
        Car::Msg writeReq{}, writeResp{};
        writeReq.cmd_type = Car::CmdType::WRITE;
        writeReq.item_id = itemId;
        writeReq.value.u8 = itemId * 10;
        module.testProcessCommand(writeReq, writeResp);

        Car::Msg readReq{}, readResp{};
        readReq.cmd_type = Car::CmdType::READ;
        readReq.item_id = itemId;
        module.testProcessCommand(readReq, readResp);
        EXPECT_EQ(readResp.value.u8, itemId * 10) << "item_id=" << static_cast<int>(itemId);
    }
}

// ========== 响应元数据测试 ==========

TEST_F(DoorModuleTest, ResponseHasCorrectModuleId) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.mod_id, Car::ModuleID::DOOR);
}

TEST_F(DoorModuleTest, ResponseResultIsZero) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.result, 0);
}

TEST_F(DoorModuleTest, WriteResponseCopiesValue) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 42;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.value.u8, 42);
}
