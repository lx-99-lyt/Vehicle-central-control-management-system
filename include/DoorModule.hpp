#pragma once
#include "ModuleServer.hpp"

class DoorModule : public ModuleServer {
    Car::DoorState m_state{};
public:
    DoorModule() : ModuleServer(Car::SOCK_DOOR, "Car_Door") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::DOOR;
        resp.result = 0;
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if (req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.front_left = req.value.u8;
            else if (req.item_id == 2) m_state.front_right = req.value.u8;
            else if (req.item_id == 3) m_state.back_left = req.value.u8;
            else if (req.item_id == 4) m_state.back_right = req.value.u8;
            else if (req.item_id == 5) m_state.trunk = req.value.u8;
            else if (req.item_id == 6) m_state.lock_status = req.value.u8;
            resp.value = req.value;
        }
        else if (req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.front_left;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.front_right;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.back_left;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.back_right;
            }
            else if (req.item_id == 5) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.trunk;
            }
            else if (req.item_id == 6) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.lock_status;
            }
        }
    }
};
