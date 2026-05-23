/** Copyright 2024 William L Thomson Jr <w@wltjr.com>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *
 *  Updated for Pi5 / ROS Robot Controller (STM32) board.
 *  All motor and servo commands are now sent through the Board serial class
 *  (0xAA 0x55 packet protocol) instead of direct I2C register writes.
 *
 *  Motor inversion logic is preserved from the Pi4 version:
 *    - Odd motor ids (1, 3) are on the left side → speed is negated so that
 *      positive effort = forward on both sides, matching the Pi5 mecanum.py
 *      pattern: board.set_motor_duty([[1, -v1], [2, v2], [3, -v3], [4, v4]])
 *
 *  Servo PWM pulse calculation is preserved from the Pi4 Board.py:
 *    pulse = ((200 * angle) / 9) + 500
 */

#include <stdlib.h>
#include <math.h>
#include <stdexcept>

#include "rclcpp/rclcpp.hpp"

#include "joint.hpp"

namespace turbopi
{
    Joint::Joint() = default;

    Joint::Joint(uint8_t id, uint8_t type, Board &board)
        : board_(&board), id_(id), type_(type)
    {
    }

    Joint::~Joint() = default;

    void Joint::setType(uint8_t type)
    {
        type_ = type;
    }

    uint8_t Joint::getId()
    {
        return id_;
    }

    double Joint::getValue()
    {
        // The Pi5 STM32 board does not expose encoder counts over the serial
        // protocol used here.  Return the last commanded value so that the
        // hardware interface can report plausible odometry.
        return _previousEffort;
    }

    void Joint::actuate(double effort, uint8_t /*duration*/)
    {
        if (type_ == TYPE_MOTOR)
        {
            float duty = 0.0f;

            if (floor(effort) != 0)
            {
                const float LOW = 50.0f;

                duty = static_cast<float>(ceil(effort) / 31.0 * 100.0);

                if (duty > 100.0f)
                    duty = 100.0f;
                else if (duty < -100.0f)
                    duty = -100.0f;
                else if (duty > 0.0f && duty < LOW)
                    duty = LOW;
                else if (duty < 0.0f && duty > -LOW)
                    duty = -LOW;

                // Invert left-side motors (ids 1 and 3 are odd).
                // Matches Pi5 mecanum.py: set_motor_duty([[1,-v1],[2,v2],[3,-v3],[4,v4]])
                if (id_ & 1u)
                    duty = -duty;
            }

            board_->setMotorDuty({{id_, duty}});

            RCLCPP_DEBUG(rclcpp::get_logger(CLASS_NAME),
                         "motor %u: effort=%.3f duty=%.1f", id_, effort, duty);
        }
        else if (type_ == TYPE_SERVO)
        {
            if (effort != _previousEffort)
            {
                // Map effort [-1..1] → angle [0..180]
                uint8_t angle = static_cast<uint8_t>(effort * 90.0 + 90.0);

                if (angle > 180)
                    angle = 180;
                if (angle > max_)
                    angle = max_;
                if (angle < min_)
                    angle = min_;

                // Pulse width in microseconds (same formula as Pi4 Board.py)
                uint16_t pulse = static_cast<uint16_t>(((200u * angle) / 9u) + 500u);

                // PWM servo id on the Pi5 board is 1-based (id_ 5→1, 6→2)
                uint8_t servo_id = id_ - 4u;

                board_->pwmServoSetPosition(0.1f, {{servo_id, pulse}});

                RCLCPP_DEBUG(rclcpp::get_logger(CLASS_NAME),
                             "servo %u (board id %u): effort=%.3f angle=%u° pulse=%u µs",
                             id_, servo_id, effort, angle, pulse);
            }
        }

        _previousEffort = effort;
    }

    void Joint::setLimits(uint8_t min, uint8_t max)
    {
        min_ = min;
        max_ = max;
    }

    double Joint::getPreviousEffort()
    {
        return _previousEffort;
    }

    int Joint::getType()
    {
        return type_;
    }
}
