/** Copyright 2025 William L Thomson Jr <w@wltjr.com>
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *
 *  Updated for Pi5 / ROS Robot Controller (STM32) board.
 *  Battery voltage is now read from the STM32 via the Board serial class.
 *  The STM32 firmware periodically broadcasts a SYS packet (sub-cmd 0x04)
 *  containing the battery voltage in millivolts (uint16_t, little-endian).
 *  Board::getBattery() returns that value; we divide by 1000 to get Volts.
 */

#include <chrono>
#include <thread>

#include "rclcpp/rclcpp.hpp"

#include "battery.hpp"

char const* const CLASS_NAME = "Battery";

namespace turbopi
{
    Battery::Battery() = default;

    Battery::Battery(Board &board) :
        board_(&board)
    {
    }

    Battery::~Battery() = default;

    float Battery::getVoltage()
    {
        if (board_ == nullptr)
            return 0.0f;

        // Wait up to 3 seconds for the STM32 to broadcast a battery reading.
        // The firmware sends SYS packets at ~2 Hz when reception is enabled.
        int mv = -1;
        for (int attempt = 0; attempt < 30 && mv < 0; ++attempt)
        {
            mv = board_->getBattery();
            if (mv < 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (mv < 0)
        {
            RCLCPP_WARN(rclcpp::get_logger(CLASS_NAME),
                        "Battery voltage not yet available from STM32");
            return 0.0f;
        }

        return static_cast<float>(mv) / 1000.0f;
    }

}
