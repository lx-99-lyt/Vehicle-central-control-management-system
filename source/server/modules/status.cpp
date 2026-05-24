// Status Module —— 车辆状态监控子进程
#include "ModuleServer.hpp"

class StatusModule : public ModuleServer {
    Car::StatusState m_state{};
public:
    StatusModule() : ModuleServer(Car::SOCK_STATUS, "Car_Status") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::STATUS;
        resp.result = 0;
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if (req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.speed = req.value.f32;
            else if (req.item_id == 2) m_state.rpm = req.value.i32;
            else if (req.item_id == 3) m_state.water_temp = req.value.f32;
            else if (req.item_id == 4) m_state.oil_temp = req.value.f32;
            else if (req.item_id == 5) m_state.fuel = req.value.f32;
            else if (req.item_id == 6) m_state.battery_voltage = req.value.f32;
            else if (req.item_id == 7) m_state.gear = req.value.u8;
            else if (req.item_id == 8) m_state.hand_brake = req.value.u8;
            resp.value = req.value;
        }
        else if (req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.speed;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::I32;
                resp.value.i32 = m_state.rpm;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.water_temp;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.oil_temp;
            }
            else if (req.item_id == 5) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.fuel;
            }
            else if (req.item_id == 6) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.battery_voltage;
            }
            else if (req.item_id == 7) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.gear;
            }
            else if (req.item_id == 8) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.hand_brake;
            }
        }
    }
};

int main() { setupModuleSignalHandlers(); StatusModule().start(); return 0; }
