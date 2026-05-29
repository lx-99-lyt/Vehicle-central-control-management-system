#include <gtest/gtest.h>
#include "FaultModule.hpp"
#include <cstring>

class FaultModuleTest : public ::testing::Test {
protected:
    class TestableFaultModule : public FaultModule {
    public:
        void testProcessCommand(const Car::Msg& request, Car::Msg& response) {
            processCommand(request, response);
        }
    };

    TestableFaultModule module;
    Car::Msg req{};
    Car::Msg resp{};

    void SetUp() override {
        req.magic = Car::CAR_MSG_MAGIC;
        req.version = Car::CAR_MSG_VERSION;
        req.mod_id = Car::ModuleID::FAULT;
        req.msg_type = Car::MsgType::CMD;
    }
};

// ========== GET_ALL 测试 ==========

TEST_F(FaultModuleTest, GetAllReturnsAllFields) {
    req.cmd_type = Car::CmdType::GET_ALL;
    module.testProcessCommand(req, resp);

    EXPECT_EQ(resp.mod_id, Car::ModuleID::FAULT);
    EXPECT_EQ(resp.result, 0);
    EXPECT_EQ(resp.val_type, Car::ValType::STR_U8);
}

// ========== WRITE + READ 联合测试 ==========

TEST_F(FaultModuleTest, WriteReadFaultCount) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 3;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 3);
}

TEST_F(FaultModuleTest, WriteReadFaultCodes) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 2;
    memset(req.value.arr_u16, 0, sizeof(req.value.arr_u16));
    req.value.arr_u16[0] = 1001;
    req.value.arr_u16[1] = 1002;
    req.value.arr_u16[2] = 1003;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::STR_U16);
    EXPECT_EQ(readResp.value.arr_u16[0], 1001);
    EXPECT_EQ(readResp.value.arr_u16[1], 1002);
    EXPECT_EQ(readResp.value.arr_u16[2], 1003);
}

TEST_F(FaultModuleTest, WriteReadWringLight) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 3;
    req.value.u8 = 1;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.val_type, Car::ValType::U8);
    EXPECT_EQ(readResp.value.u8, 1);
}

// ========== READ 初始值测试 ==========

TEST_F(FaultModuleTest, ReadInitialFaultCountIsZero) {
    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 0);
}

TEST_F(FaultModuleTest, ReadInitialFaultCodesAreZero) {
    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    for (int i = 0; i < Car::MAX_FAULT_CODE; ++i) {
        EXPECT_EQ(readResp.value.arr_u16[i], 0) << "index=" << i;
    }
}

TEST_F(FaultModuleTest, ReadInitialWringLightIsZero) {
    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 3;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 0);
}

// ========== 故障码数组测试 ==========

TEST_F(FaultModuleTest, WriteAllFaultCodes) {
    Car::Msg writeReq{}, writeResp{};
    writeReq.cmd_type = Car::CmdType::WRITE;
    writeReq.item_id = 2;
    for (int i = 0; i < Car::MAX_FAULT_CODE; ++i) {
        writeReq.value.arr_u16[i] = static_cast<uint16_t>(2000 + i);
    }
    module.testProcessCommand(writeReq, writeResp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    for (int i = 0; i < Car::MAX_FAULT_CODE; ++i) {
        EXPECT_EQ(readResp.value.arr_u16[i], static_cast<uint16_t>(2000 + i)) << "index=" << i;
    }
}

TEST_F(FaultModuleTest, FaultCodeMaxValue) {
    Car::Msg writeReq{}, writeResp{};
    writeReq.cmd_type = Car::CmdType::WRITE;
    writeReq.item_id = 2;
    memset(writeReq.value.arr_u16, 0, sizeof(writeReq.value.arr_u16));
    writeReq.value.arr_u16[0] = 65535;
    module.testProcessCommand(writeReq, writeResp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 2;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.arr_u16[0], 65535);
}

// ========== 故障计数边界值 ==========

TEST_F(FaultModuleTest, FaultCountMax) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 255;
    module.testProcessCommand(req, resp);

    Car::Msg readReq{}, readResp{};
    readReq.cmd_type = Car::CmdType::READ;
    readReq.item_id = 1;
    module.testProcessCommand(readReq, readResp);
    EXPECT_EQ(readResp.value.u8, 255);
}

// ========== 响应元数据 ==========

TEST_F(FaultModuleTest, ResponseHasCorrectModuleId) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.mod_id, Car::ModuleID::FAULT);
}

TEST_F(FaultModuleTest, ResponseResultIsZero) {
    req.cmd_type = Car::CmdType::READ;
    req.item_id = 1;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.result, 0);
}

TEST_F(FaultModuleTest, WriteResponseCopiesValue) {
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 1;
    req.value.u8 = 5;
    module.testProcessCommand(req, resp);
    EXPECT_EQ(resp.value.u8, 5);
}
