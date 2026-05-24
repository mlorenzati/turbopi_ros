// Copyright 2021 ros2_control Development Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

#include "turbopi_hardware_interface.hpp"
#include "turbopi.hpp"

#include "pluginlib/class_list_macros.hpp"

// Logger name for this hardware interface plugin
static char const* const CLASS_NAME = "TurboPiSystemHardware";
PLUGINLIB_EXPORT_CLASS(turbopi_hardware_interface::TurboPiSystemHardware,
                       hardware_interface::SystemInterface)

namespace turbopi_hardware_interface
{
    TurboPiSystemHardware::~TurboPiSystemHardware()
    {
        // Do NOT call on_deactivate() here – the ros2_control framework already
        // calls it during shutdown, and calling it again from the destructor
        // causes a second hw_stop_sec_ wait and a SIGKILL from the launch system.
    }

    hardware_interface::CallbackReturn TurboPiSystemHardware::on_init(
         const hardware_interface::HardwareComponentInterfaceParams &params)
    {
        if (hardware_interface::SystemInterface::on_init(params) !=
            hardware_interface::CallbackReturn::SUCCESS)
        {
            return hardware_interface::CallbackReturn::ERROR;
        }
        turbopi_ = turbopi::TurboPi();

        // need to add try/catch for missing params, blows up otherwise
        hw_start_sec_ = std::stod(info_.hardware_parameters["hw_start_duration_sec"]);
        hw_stop_sec_ = std::stod(info_.hardware_parameters["hw_stop_duration_sec"]);

        // Resize vectors
        hw_commands_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
        hw_positions_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
        hw_velocities_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());

        for (const hardware_interface::ComponentInfo &joint : info_.joints)
        {
            // TurboPiSystem has exactly two states and one command interface on each joint
            if (joint.command_interfaces.size() != 1)
            {
                RCLCPP_FATAL(rclcpp::get_logger(CLASS_NAME),
                             "Joint '%s' has %zu command interfaces found. 1 expected.",
                             joint.name.c_str(),
                             joint.command_interfaces.size());
                return hardware_interface::CallbackReturn::ERROR;
            }

            if (joint.name.find("wheel") != std::string::npos &&
                joint.command_interfaces[0].name != hardware_interface::HW_IF_VELOCITY)
            {
                RCLCPP_FATAL(rclcpp::get_logger(CLASS_NAME),
                             "Joint '%s' have %s command interfaces found. '%s' expected.",
                             joint.name.c_str(),
                             joint.command_interfaces[0].name.c_str(),
                             hardware_interface::HW_IF_VELOCITY);
                return hardware_interface::CallbackReturn::ERROR;
            }

            if (joint.name.find("wheel") != std::string::npos &&
                joint.state_interfaces.size() != 2)
            {
                RCLCPP_FATAL(rclcpp::get_logger(CLASS_NAME),
                             "Joint '%s' has %zu state interface. 2 expected.",
                             joint.name.c_str(),
                             joint.state_interfaces.size());
                return hardware_interface::CallbackReturn::ERROR;
            }

            if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION)
            {
                RCLCPP_FATAL(rclcpp::get_logger(CLASS_NAME),
                             "Joint '%s' have '%s' as first state interface. '%s' expected.",
                             joint.name.c_str(),
                             joint.state_interfaces[0].name.c_str(),
                             hardware_interface::HW_IF_POSITION);
                return hardware_interface::CallbackReturn::ERROR;
            }

            if (joint.name.find("wheel") != std::string::npos &&
                joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY)
            {
                RCLCPP_FATAL(rclcpp::get_logger(CLASS_NAME),
                             "Joint '%s' have '%s' as second state interface. '%s' expected.",
                             joint.name.c_str(),
                             joint.state_interfaces[1].name.c_str(),
                             hardware_interface::HW_IF_VELOCITY);
                return hardware_interface::CallbackReturn::ERROR;
            }
        }

        // Create a standalone node + publisher for raw battery mV so that
        // battery_node (separate process) can subscribe instead of opening /dev/rrc.
        battery_node_ = rclcpp::Node::make_shared("turbopi_battery_bridge");
        battery_pub_  = battery_node_->create_publisher<std_msgs::msg::Int32>(
                            "/battery_voltage_mv",
                            rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());

        return hardware_interface::CallbackReturn::SUCCESS;
    }

    std::vector<hardware_interface::StateInterface> TurboPiSystemHardware::export_state_interfaces()
    {
        std::vector<hardware_interface::StateInterface> state_interfaces;
        for (auto i = 0u; i < info_.joints.size(); i++)
        {
            state_interfaces.emplace_back(
                hardware_interface::StateInterface(info_.joints[i].name,
                                                   hardware_interface::HW_IF_POSITION,
                                                   &hw_positions_[i]));
           if(info_.joints[i].name.find("wheel") != std::string::npos)
           {
                state_interfaces.emplace_back(
                    hardware_interface::StateInterface(info_.joints[i].name,
                                                       hardware_interface::HW_IF_VELOCITY,
                                                       &hw_velocities_[i]));
           }
        }

        return state_interfaces;
    }

    std::vector<hardware_interface::CommandInterface> TurboPiSystemHardware::export_command_interfaces()
    {
        std::vector<hardware_interface::CommandInterface> command_interfaces;
        for (auto i = 0u; i < info_.joints.size(); i++)
        {
            if (info_.joints[i].name.find("wheel") != std::string::npos)
            {
                command_interfaces.emplace_back(
                    hardware_interface::CommandInterface(info_.joints[i].name,
                                                        hardware_interface::HW_IF_VELOCITY,
                                                        &hw_commands_[i]));
            }
            else
            {
                command_interfaces.emplace_back(
                    hardware_interface::CommandInterface(info_.joints[i].name,
                                                        hardware_interface::HW_IF_POSITION,
                                                        &hw_commands_[i]));
            }
        }

        return command_interfaces;
    }

    hardware_interface::CallbackReturn TurboPiSystemHardware::on_activate(
        const rclcpp_lifecycle::State & /*previous_state*/)
    {
        RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME), "Activating ...please wait...");

        for (int i = 0; i < static_cast<int>(hw_start_sec_); i++)
        {
            rclcpp::sleep_for(std::chrono::seconds(1));
            RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME),
                        "%.1f seconds left...", hw_start_sec_ - i);
        }

        // set default values
        for (auto i = 0u; i < hw_positions_.size(); i++)
        {
            if (std::isnan(hw_positions_[i]))
            {
                hw_positions_[i] = 0;
                hw_velocities_[i] = 0;
                hw_commands_[i] = 0;
            }
        }

        RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME), "Successfully activated!");

        return hardware_interface::CallbackReturn::SUCCESS;
    }

    hardware_interface::CallbackReturn TurboPiSystemHardware::on_deactivate(
        const rclcpp_lifecycle::State & /*previous_state*/)
    {
        RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME), "Deactivating ...please wait...");

        for (int i = 0; i < static_cast<int>(hw_stop_sec_); i++)
        {
            rclcpp::sleep_for(std::chrono::seconds(1));
            RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME),
                        "%.1f seconds left...", hw_stop_sec_ - i);
        }

        RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME), "Successfully deactivated!");

        return hardware_interface::CallbackReturn::SUCCESS;
    }

    hardware_interface::return_type TurboPiSystemHardware::read(
        const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
    {
        for (std::size_t i = 0; i < hw_velocities_.size(); i++)
        {
            turbopi::Joint joint = turbopi_.getJoint(info_.joints[i].name);

            if (joint.getType() == TYPE_MOTOR)
            {
                hw_positions_[i] = joint.getValue();

                RCLCPP_DEBUG(rclcpp::get_logger(CLASS_NAME),
                            "Joint: %s, command %.5f, position state: %.5f, velocity state: %.5f",
                            info_.joints[i].name.c_str(),
                            hw_commands_[i],
                            hw_positions_[i],
                            hw_velocities_[i]);
            } 

            turbopi_.setJoint(joint);
        }

        // Publish battery mV every ~5 s (50 cycles at 10 Hz) so battery_node
        // can subscribe instead of opening /dev/rrc in a separate process.
        if (battery_pub_ && (++battery_pub_counter_ >= 50))
        {
            battery_pub_counter_ = 0;
            int mv = turbopi_.getBattery();
            RCLCPP_INFO(rclcpp::get_logger(CLASS_NAME),
                        "Battery read: %d mV", mv);
            auto msg = std_msgs::msg::Int32();
            msg.data = mv;
            battery_pub_->publish(msg);
            // spin_some lets the DDS middleware process subscriber-matching events
            // so that transient-local late-joiners (battery_node) receive the retained value.
            rclcpp::spin_some(battery_node_);
        }

        return hardware_interface::return_type::OK;
    }

    hardware_interface::return_type turbopi_hardware_interface::TurboPiSystemHardware::write(
        const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
    {
        for (auto i = 0u; i < info_.joints.size(); i++)
        {
            const uint8_t duration = 1;

            turbopi::Joint joint = turbopi_.getJoint(info_.joints[i].name);

            hw_velocities_[i] = hw_commands_[i];

            // Commands sent to hardware
            RCLCPP_DEBUG(rclcpp::get_logger(CLASS_NAME),
                         "Joint: %s, command %.5f, position state: %.5f, velocity state: %.5f",
                         info_.joints[i].name.c_str(),
                         hw_commands_[i],
                         hw_positions_[i],
                         hw_velocities_[i]);

            joint.actuate(hw_commands_[i], duration);
        }

        return hardware_interface::return_type::OK;
    }

}

