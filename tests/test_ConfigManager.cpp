#include <gtest/gtest.h>
#include "ConfigManager.hpp"
#include <cstdio>
#include <fstream>
#include <filesystem>

class ConfigManagerTest : public ::testing::Test {
protected:
    static constexpr const char* TEST_INI = "./test_car_info.ini";

    void SetUp() override {
        // 备份原始路径常量，使用临时文件
        std::filesystem::remove(TEST_INI);
    }

    void TearDown() override {
        std::filesystem::remove(TEST_INI);
        std::filesystem::remove(std::string(TEST_INI) + ".tmp");
    }

    // 辅助：写入测试配置文件
    void writeTestConfig(const std::string& content) {
        std::ofstream out(TEST_INI);
        out << content;
        out.close();
    }
};

// ========== initDefaults 测试 ==========

TEST_F(ConfigManagerTest, InitDefaultsSetsExpectedValues) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();
    auto data = mgr.getData();

    EXPECT_FLOAT_EQ(data.status.battery_voltage, 12.0f);
    EXPECT_FLOAT_EQ(data.status.fuel, 50.0f);
    EXPECT_EQ(data.status.gear, static_cast<uint8_t>(Car::Gear::P));
    EXPECT_EQ(data.status.hand_brake, 1);
    EXPECT_FLOAT_EQ(data.status.oil_temp, 40.0f);
    EXPECT_FLOAT_EQ(data.status.water_temp, 30.0f);
    EXPECT_EQ(data.air.temp_set, 20);
}

TEST_F(ConfigManagerTest, InitDefaultsZeroesOtherFields) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();
    auto data = mgr.getData();

    // 未在 initDefaults 中设置的字段应保持零初始化
    EXPECT_EQ(data.door.front_left, 0);
    EXPECT_EQ(data.door.front_right, 0);
    EXPECT_EQ(data.door.back_left, 0);
    EXPECT_EQ(data.door.back_right, 0);
    EXPECT_EQ(data.door.trunk, 0);
    EXPECT_EQ(data.door.lock_status, 0);
    EXPECT_FLOAT_EQ(data.status.speed, 0.0f);
    EXPECT_EQ(data.status.rpm, 0);
    EXPECT_EQ(data.air.ac_switch, 0);
    EXPECT_EQ(data.air.fan_speed, 0);
    EXPECT_EQ(data.air.inner_cycle, 0);
    EXPECT_EQ(data.fault.fault_count, 0);
    EXPECT_EQ(data.fault.wring_light, 0);
}

// ========== getData / setData 测试 ==========

TEST_F(ConfigManagerTest, SetDataThenGetData) {
    auto& mgr = ConfigManager::getInstance();
    ConfigManager::FullCarData data{};
    data.door.front_left = 1;
    data.door.lock_status = 1;
    data.status.speed = 60.5f;
    data.status.rpm = 3000;
    data.air.ac_switch = 1;
    data.air.temp_set = 25;
    data.fault.fault_count = 2;
    data.fault.fault_codes[0] = 1001;
    data.fault.fault_codes[1] = 1002;

    mgr.setData(data);
    auto loaded = mgr.getData();

    EXPECT_EQ(loaded.door.front_left, 1);
    EXPECT_EQ(loaded.door.lock_status, 1);
    EXPECT_FLOAT_EQ(loaded.status.speed, 60.5f);
    EXPECT_EQ(loaded.status.rpm, 3000);
    EXPECT_EQ(loaded.air.ac_switch, 1);
    EXPECT_EQ(loaded.air.temp_set, 25);
    EXPECT_EQ(loaded.fault.fault_count, 2);
    EXPECT_EQ(loaded.fault.fault_codes[0], 1001);
    EXPECT_EQ(loaded.fault.fault_codes[1], 1002);
}

TEST_F(ConfigManagerTest, SetDataOverwritesPrevious) {
    auto& mgr = ConfigManager::getInstance();

    ConfigManager::FullCarData data1{};
    data1.status.speed = 100.0f;
    mgr.setData(data1);

    ConfigManager::FullCarData data2{};
    data2.status.speed = 200.0f;
    mgr.setData(data2);

    EXPECT_FLOAT_EQ(mgr.getData().status.speed, 200.0f);
}

// ========== save / load 测试 ==========

TEST_F(ConfigManagerTest, SaveAndLoadRoundtrip) {
    // 使用临时文件进行 save/load 测试
    // 注意：ConfigManager 使用固定的 INI_FILE_PATH，这里测试 save 后 load 的一致性
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    ConfigManager::FullCarData data{};
    data.door.front_left = 1;
    data.door.trunk = 1;
    data.status.speed = 88.5f;
    data.status.rpm = 4500;
    data.status.gear = static_cast<uint8_t>(Car::Gear::D);
    data.air.ac_switch = 1;
    data.air.fan_speed = 5;
    data.air.temp_set = 22;
    data.fault.fault_count = 1;
    data.fault.fault_codes[0] = 999;
    data.fault.wring_light = 1;
    mgr.setData(data);

    // save 会写入 Car::INI_FILE_PATH
    bool saved = mgr.save();
    EXPECT_TRUE(saved);

    // 修改数据
    ConfigManager::FullCarData empty{};
    mgr.setData(empty);

    // load 应该恢复之前保存的数据
    bool loaded = mgr.load();
    EXPECT_TRUE(loaded);

    auto restored = mgr.getData();
    EXPECT_EQ(restored.door.front_left, 1);
    EXPECT_EQ(restored.door.trunk, 1);
    EXPECT_FLOAT_EQ(restored.status.speed, 88.5f);
    EXPECT_EQ(restored.status.rpm, 4500);
    EXPECT_EQ(restored.status.gear, static_cast<uint8_t>(Car::Gear::D));
    EXPECT_EQ(restored.air.ac_switch, 1);
    EXPECT_EQ(restored.air.fan_speed, 5);
    EXPECT_EQ(restored.air.temp_set, 22);
    EXPECT_EQ(restored.fault.fault_count, 1);
    EXPECT_EQ(restored.fault.fault_codes[0], 999);
    EXPECT_EQ(restored.fault.wring_light, 1);

    // 清理
    std::filesystem::remove(Car::INI_FILE_PATH);
    std::filesystem::remove(std::string(Car::INI_FILE_PATH) + ".tmp");
}

TEST_F(ConfigManagerTest, LoadNonExistentFileReturnsFalse) {
    auto& mgr = ConfigManager::getInstance();
    // 确保文件不存在
    std::filesystem::remove(Car::INI_FILE_PATH);
    EXPECT_FALSE(mgr.load());
}

TEST_F(ConfigManagerTest, LoadMalformedConfigDoesNotCrash) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    // 写入格式错误的配置
    std::ofstream out(Car::INI_FILE_PATH);
    out << "this is not valid ini\n";
    out << "no equals sign here\n";
    out << "[door\n";  // 缺少右括号
    out << "===\n";    // 多个等号
    out << " = \n";    // 空 key 和 value
    out.close();

    // load 不应崩溃
    EXPECT_TRUE(mgr.load());

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, LoadWithInvalidValuesIgnored) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "[door]\n";
    out << "door_front_left = not_a_number\n";
    out << "door_front_right = 99999999999999999999\n";  // 溢出
    out.close();

    // load 应该跳过无效值而不崩溃
    EXPECT_TRUE(mgr.load());

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, LoadWithCommentsAndBlankLines) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "# This is a comment\n";
    out << "; This is also a comment\n";
    out << "\n";
    out << "   \n";
    out << "[door]\n";
    out << "\n";
    out << "door_front_left = 1\n";
    out << "# another comment\n";
    out << "lock_status = 1\n";
    out.close();

    EXPECT_TRUE(mgr.load());
    auto data = mgr.getData();
    EXPECT_EQ(data.door.front_left, 1);
    EXPECT_EQ(data.door.lock_status, 1);

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, LoadGearTextValues) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "[status]\n";
    out << "gear = R\n";
    out.close();

    EXPECT_TRUE(mgr.load());
    EXPECT_EQ(mgr.getData().status.gear, static_cast<uint8_t>(Car::Gear::R));

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, LoadGearCodeValues) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "[status]\n";
    out << "gear_code = 3\n";
    out.close();

    EXPECT_TRUE(mgr.load());
    EXPECT_EQ(mgr.getData().status.gear, static_cast<uint8_t>(Car::Gear::D));

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, LoadAllSections) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "[door]\n";
    out << "door_front_left = 1\n";
    out << "door_front_right = 1\n";
    out << "door_back_left = 0\n";
    out << "door_back_right = 0\n";
    out << "door_trunk = 1\n";
    out << "lock_status = 1\n";
    out << "[status]\n";
    out << "speed = 120.5\n";
    out << "rpm = 6000\n";
    out << "water_temp = 90.5\n";
    out << "oil_temp = 85.0\n";
    out << "fuel = 75.0\n";
    out << "battery_voltage = 12.8\n";
    out << "gear = D\n";
    out << "hand_brake = 0\n";
    out << "[air]\n";
    out << "ac_switch = 1\n";
    out << "fan_speed = 4\n";
    out << "temp_set = 24\n";
    out << "inner_cycle = 1\n";
    out << "[fault]\n";
    out << "fault_count = 2\n";
    out << "wring_light = 1\n";
    out << "fault_code_0 = 1001\n";
    out << "fault_code_1 = 1002\n";
    out.close();

    EXPECT_TRUE(mgr.load());
    auto d = mgr.getData();

    EXPECT_EQ(d.door.front_left, 1);
    EXPECT_EQ(d.door.front_right, 1);
    EXPECT_EQ(d.door.back_left, 0);
    EXPECT_EQ(d.door.lock_status, 1);
    EXPECT_FLOAT_EQ(d.status.speed, 120.5f);
    EXPECT_EQ(d.status.rpm, 6000);
    EXPECT_FLOAT_EQ(d.status.water_temp, 90.5f);
    EXPECT_EQ(d.status.gear, static_cast<uint8_t>(Car::Gear::D));
    EXPECT_EQ(d.status.hand_brake, 0);
    EXPECT_EQ(d.air.ac_switch, 1);
    EXPECT_EQ(d.air.fan_speed, 4);
    EXPECT_EQ(d.air.temp_set, 24);
    EXPECT_EQ(d.air.inner_cycle, 1);
    EXPECT_EQ(d.fault.fault_count, 2);
    EXPECT_EQ(d.fault.wring_light, 1);
    EXPECT_EQ(d.fault.fault_codes[0], 1001);
    EXPECT_EQ(d.fault.fault_codes[1], 1002);

    std::filesystem::remove(Car::INI_FILE_PATH);
}

TEST_F(ConfigManagerTest, FaultCodeIndexBounds) {
    auto& mgr = ConfigManager::getInstance();
    mgr.initDefaults();

    std::ofstream out(Car::INI_FILE_PATH);
    out << "[fault]\n";
    out << "fault_code_0 = 111\n";
    out << "fault_code_9 = 999\n";
    out << "fault_code_10 = 888\n";  // 超出 MAX_FAULT_CODE，应被忽略
    out << "fault_code_-1 = 777\n";  // 负索引，stoi 会解析为 -1，应被忽略
    out.close();

    EXPECT_TRUE(mgr.load());
    auto d = mgr.getData();
    EXPECT_EQ(d.fault.fault_codes[0], 111);
    EXPECT_EQ(d.fault.fault_codes[9], 999);

    std::filesystem::remove(Car::INI_FILE_PATH);
}
