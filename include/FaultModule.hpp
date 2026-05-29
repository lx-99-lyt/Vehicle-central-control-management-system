#pragma once
#include "ModuleServer.hpp"

class FaultModule : public ModuleServer {
    Car::FaultState m_state{};
public:
    FaultModule() : ModuleServer(Car::SOCK_FAULT, "Car_Fault") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::FAULT;
        resp.result = 0;
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if (req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.fault_count = req.value.u8;
            else if (req.item_id == 2) std::memcpy(m_state.fault_codes, req.value.arr_u16, sizeof(m_state.fault_codes));
            else if (req.item_id == 3) m_state.wring_light = req.value.u8;
            resp.value = req.value;
        }
        else if (req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.fault_count;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::STR_U16;
                std::memcpy(resp.value.arr_u16, m_state.fault_codes, sizeof(m_state.fault_codes));
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.wring_light;
            }
        }
    }
};
