# TurboPi ROS - pi 5 board version

<img width="1672" height="941" alt="image" src="https://github.com/user-attachments/assets/15b94b0e-b6e3-4221-b974-ab5776f6b17d" />
"Everything looks better with a Commodore Amiga on the back"

## Notes 
This project is a collaboration to the work done by [Whilliam L Thomson](https://github.com/wltjr) to support into [TurboPi](https://github.com/wltjr/turbopi_ros) the new board for the raspberry pi 5.

Licenses and credits fully goes to the original work creator and the plan is to move it back there with colcon compilation options passing the board for rpi4 or 5

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg?style=plastic)](https://github.com/wltjr/turbopi_ros/blob/master/LICENSE.txt)
![Build Status](https://github.com/wltjr/turbopi_ros/actions/workflows/docker_build.yml/badge.svg)
[![Code Quality](https://sonarcloud.io/api/project_badges/measure?project=wltjr_turbopi_ros&metric=alert_status)](https://sonarcloud.io/dashboard?id=wltjr_turbopi_ros)

This project aims to get all functionality of the TurboPi robot running on
[ROS 2 Jazzy](https://docs.ros.org/en/jazzy/), along with an RPLidar for SLAM.
The project is a work in progress, used to learn ROS 2, as well as for upcoming
research work into SLAM, navigation without GPS or Compass, dynamic path planning
with encountering unknown newly discovered obstacles, and other research topics.

Work is underway to customize the default TurboPi, replacing the 2DOF camera
with a Orbbec Astra S 3D Depth Camera. This will be done in a manner that
supports both, and work has already be done for simulation of both. A 2D
360&#176; RP LiDAR A1 has been added to both.

<table style="padding:10px">
  <tr>
    <td> 
      <img src="https://github.com/user-attachments/assets/7a3572b8-a915-4015-9751-5b90ae2336e1" alt="TurboPi in Gazebo" width="265px" height="255px" >
    </td>
    <td>
      <img src="https://github.com/user-attachments/assets/92f3f31c-802f-4783-b9ac-52aefce8a10d" alt="Custom TurboPi in Gazebo" width="265px" height="255px" >
    </td>
  </tr>
</table>

[![2024-12-06 Turbopi_ROS Autonomous Nav & Obstacle Avoidance](https://github.com/user-attachments/assets/5d47b4ba-92f1-48ec-8ad1-7371701c17a7)](https://github.com/user-attachments/assets/161aecaf-2058-4035-a6cc-9e89406dbd6d)

Video demonstration of Autonomous Navigation and Obstacle avoidance.

Mesh files have been generously provided by Hiwonder and are their property.
Mesh files are copyright Hiwonder. All Rights Reserved.

## Environment Preparation

The following assumes you have installed all the necessary ROS 2 Jazzy packages,
and have sourced the installation before running any `ros2` commands.

```bash
source /opt/ros/jazzy/setup.bash
```

You may want to have your development user environment do this on login via
`~/.bashrc` file; add that command to the end of that file.

Its recommended to have a ros workspace in `/opt/ros_ws/` for development
purposes and to build this project. The following will refer to that directory, and
directories created within. This directory matches the layout in docker containers,
and allows usage of RViz2 on a remote system, while running on actual hardware.

## Download

Download and unpack or clone this repositories contents into your ros2
workspace; ex `/opt/ros_ws/src/turbopi_ros`.

## System Setup (outside of colcon build)

The following steps are required **once** on the Raspberry Pi 5 before running
the robot. They configure the operating system so that the hardware can be
accessed correctly.

### 1. Enable GPIO UART for the ROS Robot Controller (STM32)

The Hiwonder ROS Robot Controller expansion board communicates with the Pi 5
via the **40-pin GPIO header UART** (not USB). This UART must be enabled in the
Pi 5 boot configuration.

Add the following line to `/boot/firmware/config.txt`:

```
dtoverlay=uart0-pi5
```

Or run:

```bash
echo "" | sudo tee -a /boot/firmware/config.txt
echo "# Enable GPIO UART for ROS Robot Controller (STM32) expansion board" | sudo tee -a /boot/firmware/config.txt
echo "dtoverlay=uart0-pi5" | sudo tee -a /boot/firmware/config.txt
```

A **reboot is required** after this change. Once rebooted, the UART appears as
`/dev/ttyAMA10`.

### 2. Install the udev rule to create /dev/rrc

The Board driver opens `/dev/rrc` — a symlink created by a udev rule. Install
it with the provided script (run from the workspace `src/turbopi_ros` directory):

```bash
sudo cp etc/udev/99-ttyAMA-rrc.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Or use the convenience script:

```bash
sudo bash etc/udev/install_udev_rules.sh
```

After reboot, verify the symlink exists:

```bash
ls -la /dev/rrc
# Expected: lrwxrwxrwx ... /dev/rrc -> ttyAMA10
```

### 3. Add user to dialout group

The serial port `/dev/ttyAMA10` requires the user to be in the `dialout` group:

```bash
sudo usermod -aG dialout $USER
# log out and back in (or reboot) for the group change to take effect
```

### 4. RPLidar — install udev rule for /dev/rplidar

The RPLidar A1 connects via USB using a Silicon Labs CP210x (CP2102) UART
bridge (`idVendor=10c4 idProduct=ea60`). The `install_udev_rules.sh` script
(step 2) also installs `99-rplidar.rules`, which creates the `/dev/rplidar`
symlink automatically when the lidar is plugged in.

If you installed the rules before this symlink rule was added, re-run the
install script:

```bash
sudo bash etc/udev/install_udev_rules.sh
```

Verify after plugging in the lidar:

```bash
ls -la /dev/rplidar
# Expected: lrwxrwxrwx ... /dev/rplidar -> ttyUSB0
```

If your unit uses a CH340 chip instead of CP2102 (verify with
`udevadm info -a -n /dev/ttyUSB0 | grep -E 'idVendor|idProduct'`), edit
`etc/udev/99-rplidar.rules` to use `idVendor="1a86" idProduct="7523"`.

### RPLidar mount orientation

The RPLidar A1 is physically mounted **facing backward** on the TurboPi (USB
connector toward the front of the robot). The `lidar_joint` in the URDF
compensates for this by applying a **180° (π radian) yaw rotation** by default,
so that the published `/scan` points are correctly oriented in the `map` frame.

This is controlled by the `lidar_yaw` launch argument (default `π`):

```bash
# Default — lidar USB connector faces forward (backward-facing sensor, 180° rotation)
ros2 launch turbopi_ros turbopi_ros.launch.py lidar:=True

# Override — lidar USB connector faces backward (forward-facing sensor, no rotation)
ros2 launch turbopi_ros turbopi_ros.launch.py lidar:=True lidar_yaw:=0.0
```

To verify the lidar orientation in the TF tree:

```bash
# Check the lidar joint transform
ros2 run tf2_ros tf2_echo chassis lidar
# With default lidar_yaw=pi: rotation.z should be ~1.0 (quaternion, equivalent to 180°)
```

### Automated setup

If setting up a fresh Pi 5, the `rpi5-ubuntu-jammy-setup.sh` script at the root
of this repository performs all of the above steps automatically (including
installing ROS 2 Jazzy, cloning the workspace, and building):

```bash
sudo bash rpi5-ubuntu-jammy-setup.sh
sudo reboot
```

---

## Build and Install

Building is done using colcon which will invoke cmake and run the necessary
commands. Run the following command in your ros2 workspace; ex `/opt/ros_ws/`.

```bash
colcon build --symlink-install --packages-select  turbopi_ros
```

### Source install

Make sure to run the following command after install and login. Run the
following command in your ros2 workspace; ex `/opt/ros_ws/`.

```bash
source install/setup.bash
```

You may want to have your development user environment do this on login via
`~/.bashrc` file; add the following to the end of that file.

```bash
source /opt/ros_ws/install/setup.bash
```

## Run

There are several launchers that are used to run parts of the project, some are
used together, some stand-alone, some for simulation and the robot. They are all
run from your ros2 workspace; ex `/opt/ros_ws/`.

```bash
ros2 launch turbopi_ros turbopi_ros.launch.py
```

- [gamepad.launch.py](https://github.com/wltjr/turbopi_ros/blob/main/launch2/gamepad.launch.py) -
Start the gamepad node for remote operation, teleop; run in container, local, or
remote.
- [ign_gazebo.launch.py](https://github.com/wltjr/turbopi_ros/blob/main/launch2/ign_gazebo.launch.py) -
Start a simulated TurboPi in Gazebo; run in container or desktop/laptop.
  - `depth:=False` - Run customized 3d camera vs sonar with 2d camera (default `True`).
  - `drive:=mecanum` - Drive system diff or mecanum (default `diff`).
  - `lidar:=False` - Run customized RPLidar (default `True`).
  - `world:=playground` - The world the robot will be spawned within (default `none`).
- [nav2.launch.py](https://github.com/wltjr/turbopi_ros/blob/main/launch2/nav2.launch.py) -
Start the Nav 2 stack, used with both hardware and simulation.
- [turbopi_ros.launch.py](https://github.com/wltjr/turbopi_ros/blob/main/launch2/turbopi_ros.launch.py) -
Start ROS 2 with hardware support for TurboPi on robot hardware, following
optional arguments (all default `False` unless noted).
  - `camera:=True` - Enable the v4l2 camera node (requires `v4l2_camera` package
    and a compatible USB camera on `/dev/video0`).
  - `camera_type:=default` - Camera type to use: `depth` (Orbbec Astra S, **default**)
    or `default` (original 2DOF pan/tilt camera). Setting `default` also enables
    the `position_controllers` for the pan/tilt servos. With `depth` the
    position controller spawner is skipped entirely (the joints don't exist).
  - `drive:=mecanum` - Drive system diff or mecanum (default `diff`).
  - `lidar:=True` - Enable optional hardware lidar support (RPLidar on `/dev/rplidar` udev symlink). Override port with `lidar_port:=/dev/ttyUSB0`.
  - `lidar_yaw:=3.14159` - Lidar mount yaw rotation in radians (default `π` = 180° for backward-facing mount). Use `0.0` for forward-facing mount.
  - `sim:=True` - Use simulated mock hardware (skips all serial/I2C access).

#### Common hardware launch combinations

Minimal (diff drive, no camera, no lidar):
```bash
ros2 launch turbopi_ros turbopi_ros.launch.py
```

With 2DOF USB camera and RPLidar (diff drive):
```bash
ros2 launch turbopi_ros turbopi_ros.launch.py \
    camera:=True camera_type:=default \
    lidar:=True
```

With 2DOF USB camera, RPLidar and **mecanum drive** (full hardware):
```bash
ros2 launch turbopi_ros turbopi_ros.launch.py \
    drive:=mecanum \
    camera:=True camera_type:=default \
    lidar:=True
```

Simulation only (no hardware required):
```bash
ros2 launch turbopi_ros turbopi_ros.launch.py sim:=True
```

### Docker Containers

Three docker containers have been made to aid primarily in development, but the
first can be used on actual hardware.

- [docker-ros2-jazzy](https://github.com/UNF-Robotics/docker-ros2-jazzy) -
Base headless container used in CI/CD and can be used on hardware with slight
overhead.
- [docker-ros2-jazzy-gz-rviz2](https://github.com/UNF-Robotics/docker-ros2-jazzy-gz-rviz2) -
Base X11 graphical container used for simulation has Gazebo and RViz2, intended
for desktop/laptop; contains prior.
- [docker-ros2-jazzy-gz-rviz2-turbopi](https://github.com/wltjr/docker-ros2-jazzy-gz-rviz2-turbopi) -
Main development container used for development, simulation, etc. intended for
desktop/laptop; contains prior two.

The first container is primarily used for CI/CD. The second one is not directly
used. For most purposes, the last container is the primary one to use, outside
of running on the actual robot. Which is advised to do outside of a docker
container to avoid the minimal overhead.

## Robot Human Controllers

The primary way to control the robot is using telop_turbopi which is intended to
be used with a
[DUALSHOCK™4](https://www.playstation.com/en-us/accessories/dualshock-4-wireless-controller/)
wireless controller.

### DUALSHOCK™4

Run the following command to invoke the controller for the DUALSHOCK™4 wireless
controller. This can be done within the robot, or on a remote system running a
docker container or locally installed.

```bash
ros2 launch turbopi_ros gamepad.launch.py
```

#### Button Layout

The left joystick controls driving and the right joystick controls the camera.
There is **no deadman button** — any stick movement immediately sends velocity
commands to the robot.

> ⚠️ **WARNING:** The **SHARE** button executes `sudo init 0` — it
> **shuts down the Raspberry Pi immediately**. Do not press it accidentally.

<img align="left" alt="Drawing of DUALSHOCK™4" src="https://manuals.playstation.net/document/imgps4/other_basic_018.jpg" />

| Control | Action |
| ------------- | ------------- |
| Left stick Y (up/down) | Drive forward / backward |
| Left stick X (left/right) | Rotate left / rotate right |
| Right stick Y | Camera tilt up / down |
| Right stick X | Camera pan left / right |
| SHARE button ⚠️ | **Shuts down the Raspberry Pi** (`sudo init 0`) |
| All other buttons | Unused |

> **Note:** Camera pan/tilt controls only work when launched with
> `camera_type:=default` (2DOF pan/tilt servo camera). With the depth camera
> (`camera_type:=depth`, the default) those joints do not exist.
>
> **Note:** Left stick X maps to `angular.z` (rotation), not lateral strafe.
> True mecanum sideways movement from the gamepad would require remapping
> right stick X to `twist.linear.y`.

### Alternatives

There are presently alternative two ways to control the robot using
[teleop twist joy](https://github.com/ros2/teleop_twist_joy) and
[keyboard](https://github.com/ros2/teleop_twist_keyboard). However, they only
support robot movement and do not control camera or other peripherals, they just
 use `/cmd_vel` topic.

#### Gamepad

Run the following command to invoke the controller for the gamepad. Sample
command is  using a Logitech F310, which works with the `xbox` configuration.

```bash
ros2 launch teleop_twist_joy teleop-launch.py joy_config:='xbox'
```

#### Keyboard

Run the following command to invoke the controller for the keyboard, which will
present a interface for controlling the robot in the same terminal the command
is run within.

```bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

## Camera (2DOF USB Camera)

The robot supports the original Hiwonder 2DOF pan/tilt USB camera via the
`v4l2_camera` ROS package. The camera connects to `/dev/video0` and the two
servo joints control pan (left/right) and tilt (up/down).

### 1. Verify the camera is detected

```bash
# Check the device exists
ls /dev/video*
# Expected: /dev/video0 (plus metadata nodes video1, video2)

# Check kernel recognised it (shows driver name e.g. uvcvideo)
v4l2-ctl --list-devices

# Check supported formats/resolutions
v4l2-ctl --device=/dev/video0 --list-formats-ext

# Capture a single test frame (no ROS needed)
ffmpeg -f v4l2 -i /dev/video0 -frames:v 1 /tmp/test.jpg -y && echo "Camera OK"
```

If `/dev/video0` does not appear, add your user to the `video` group:

```bash
sudo usermod -aG video $USER
# log out and back in for the change to take effect
```

### 2. Launch with camera enabled

```bash
ros2 launch turbopi_ros turbopi_ros.launch.py \
    camera:=True \
    camera_type:=default
```

- `camera:=True` — starts the `v4l2_camera_node` (default `False`)
- `camera_type:=default` — selects the 2DOF pan/tilt camera and enables the
  `position_controllers` for the pan/tilt servos (default is `depth`)

To also enable the RPLidar at the same time:

```bash
ros2 launch turbopi_ros turbopi_ros.launch.py \
    camera:=True \
    camera_type:=default \
    lidar:=True
```

### 3. Verify the camera topic

```bash
# Should show ~30 Hz when the camera is streaming
ros2 topic hz /camera

# Show topic type and publisher count
ros2 topic info /camera

# View a single compressed frame in the terminal (requires image_transport)
ros2 run image_transport republish raw --ros-args -r in:=/camera -r out:=/camera_out
```

### 4. View the live feed in RViz2

The `turbopi.rviz` config already includes an **Image** display subscribed to
`/camera`. Launch RViz2 with the pre-configured layout and the camera panel
will show the live feed automatically:

```bash
ros2 run rviz2 rviz2 -d ~/ros2_ws/install/turbopi_ros/share/turbopi_ros/config/turbopi.rviz
```

### Camera configuration

The camera parameters (resolution, pixel format, colour adjustments) are in
`config/camera.yaml`:

| Parameter | Value |
|-----------|-------|
| Device | `/dev/video0` |
| Resolution | 640 × 480 |
| Pixel format | YUYV |
| Frame ID | `camera` |
| Camera info | `config/camera_info.yaml` |

To change the resolution, edit `config/camera.yaml`:

```yaml
image_size: [1280, 720]   # change from 640x480
```

> **Note:** The 2DOF pan/tilt servo joints (`camera_pan_joint`,
> `camera_tilt_joint`) are only active when `camera_type:=default`. With the
> depth camera (`camera_type:=depth`, the default) those joints do not exist
> and `position_controllers` is not started.

---

## Sensors & Actuators

### Sonar (Ultrasonic Distance Sensor)

The sonar sensor communicates over I2C (address `0x77`, works on both Pi4 and Pi5).
It is started automatically by `turbopi_ros.launch.py`.

| Topic | Message type | Rate |
|-------|-------------|------|
| `/sonar` | `sensor_msgs/msg/Range` | ~10 Hz |

```bash
# Monitor distance readings
ros2 topic echo /sonar
```

### Infrared Line-Follower Sensors

The four-infrared sensor array communicates over I2C (address `0x78`, works on
both Pi4 and Pi5). It is started automatically by `turbopi_ros.launch.py`.

| Topic | Message type | Description |
|-------|-------------|-------------|
| `/infrared` | `std_msgs/msg/UInt8MultiArray` | 4 sensor values (0 = dark, 1 = light) |

```bash
# Monitor infrared sensor readings
ros2 topic echo /infrared
```

### Buzzer

The buzzer is driven by the STM32 ROS Robot Controller board via the same UART
link (`/dev/rrc`) used by the drive motors and servos. It supports frequency and
timing control (unlike the Pi4's simple GPIO on/off buzzer).

The buzzer is exposed as a **subscriber** topic handled inside the hardware
interface process — no extra UART process is opened.

| Topic | Message type | Description |
|-------|-------------|-------------|
| `/buzzer` | `std_msgs/msg/Float32MultiArray` | `[freq_hz, on_time_s, off_time_s, repeat]` |

**Field reference:**

| Index | Field | Example | Description |
|-------|-------|---------|-------------|
| `[0]` | `freq` | `1900.0` | Frequency in Hz (high pitch) / `400.0` (low pitch) |
| `[1]` | `on_time` | `0.1` | Beep on duration in seconds |
| `[2]` | `off_time` | `0.05` | Gap between beeps in seconds |
| `[3]` | `repeat` | `2` | Number of beep repetitions |

```bash
# Two short high-pitched beeps
ros2 topic pub --once /buzzer std_msgs/msg/Float32MultiArray \
    "data: [1900.0, 0.1, 0.05, 2]"

# One long low-pitched tone (error/warning)
ros2 topic pub --once /buzzer std_msgs/msg/Float32MultiArray \
    "data: [400.0, 2.0, 0.0, 1]"
```

#### Startup tones

The hardware interface automatically sounds the buzzer when the robot starts:

| Event | Tone |
|-------|------|
| OK    `on_activate()` success (hardware ready) | 1900 Hz · 0.1 s on · 0.05 s off · **2 beeps** |
| ERROR `on_init()` failure (URDF/joint config error) | 400 Hz · 2 s on · **1 long tone** |

---

## Hardware

- [Hiwonder TurboPi](https://www.hiwonder.com/products/turbopi?variant=40112905388119) - 
  [Amazon](https://www.amazon.com/dp/B0BTTH8WD2)

  ![Picture of Hiwonder TurboPi](https://github.com/wltjr/turbopi_ros/assets/12835340/81dd585b-5b98-43b2-b532-ddd4233721ce)

- [Orbbec Astra S](https://www.orbbec.com/products/structured-light-camera/astra-series/) -
  [Amazon](https://www.amazon.com/gp/product/B0C2H4QL5F/)

  ![Picture of Orbbec Astra S](https://github.com/user-attachments/assets/08dc402d-9d05-453e-9a04-c889ad56a590)

- [Slamtec RPlidar A1](https://www.slamtec.ai/home/rplidar_a1/) -
  [Amazon](https://www.amazon.com/dp/B07TJW5SXF/)

  ![Picture of Slamtec RPlidar A1](https://github.com/wltjr/turbopi_ros/assets/12835340/9f7b9688-b600-42d9-8b1b-c3a834252112)

## Credits

Credits and thanks for resources used in this repository, some code and/or
project structure, go to the following:

- Articulated Robotics - 
  [Making a Mobile Robot with ROS](https://articulatedrobotics.xyz/tutorials/)
- Linux I2C - [Implementing I2C device drivers in userspace](https://www.kernel.org/doc/html/latest/i2c/dev-interface.html)
- ROS 2 Control Demos -
  [example 2](https://github.com/ros-controls/ros2_control_demos)
- Slate Robotics - 
  [How to implement ros_control on a custom robot](https://slaterobotics.medium.com/how-to-implement-ros-control-on-a-custom-robot-748b52751f2e)
