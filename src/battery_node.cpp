/** Copyright 2025 William L Thomson Jr <w@wltjr.com>
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *
 *  Updated for Pi5 / ROS Robot Controller (STM32) board.
 *  Battery voltage is read from the STM32 via the Board serial class.
 */

#include <memory>

#include "battery.hpp"
#include "battery_node.hpp"

using namespace std::chrono_literals;

namespace turbopi
{

    BatteryNode::BatteryNode(const rclcpp::NodeOptions &options)
        : Node("battery", options)
    {
        // Board opens /dev/rrc and starts the background receive thread
        static auto board = Board();
        board.enableReception(true);

        static auto battery = Battery(board);
        battery_ = &battery;

        // publish to /battery topic
        publisher_battery_ =
            this->create_publisher<sensor_msgs::msg::BatteryState>("battery",
                rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());

        timer_battery_ =
            this->create_wall_timer(5s, std::bind(&BatteryNode::callbackBatteryPublisher, this));
    }

    void BatteryNode::callbackBatteryPublisher()
    {
        auto msg = std::make_unique<sensor_msgs::msg::BatteryState>();

        msg->header.stamp = rclcpp::Time(0, 0, this->get_clock()->get_clock_type());
        msg->header.frame_id = "battery";
        msg->voltage = battery_->getVoltage();
        msg->present = (msg->voltage > 0.0f);

        publisher_battery_->publish(std::move(msg));
    }
}

/**
 * @brief Node executable wrapper for the shared object
 */
int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);

    rclcpp::spin(std::make_unique<turbopi::BatteryNode>(rclcpp::NodeOptions()));

    rclcpp::shutdown();

    return 0;
}
