// Air Module —— 空调控制子进程
#include "ModuleServer.hpp"

class AirModule : public ModuleServer {
    Car::AirState m_state{};
public:
    AirModule() : ModuleServer(Car::SOCK_AIR, "Car_Air") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::AIR;
        resp.result = 0;
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if (req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.ac_switch = req.value.u8;
            else if (req.item_id == 2) m_state.fan_speed = req.value.u8;
            else if (req.item_id == 3) m_state.temp_set = req.value.i32;
            else if (req.item_id == 4) m_state.inner_cycle = req.value.u8;
            resp.value = req.value;
        }
        else if (req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.ac_switch;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.fan_speed;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::I32;
                resp.value.i32 = m_state.temp_set;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.inner_cycle;
            }
        }
    }
};

int main() { setupModuleSignalHandlers(); AirModule().start(); return 0; }
