import os
import subprocess

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, LogInfo, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnProcessExit, OnProcessStart, OnShutdown
from launch.launch_context import LaunchContext
from launch.substitutions import Command, FindExecutable, LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node


def _mecanum_drive_available() -> bool:
    """Return True if mecanum_drive_controller plugin is installed on this system."""
    try:
        import subprocess as sp
        result = sp.run(
            ["ros2", "pkg", "prefix", "mecanum_drive_controller"],
            capture_output=True, timeout=5,
        )
        return result.returncode == 0
    except Exception:
        return False


def launch_setup(context: LaunchContext):

    CM = "/controller_manager"
    pkg_name = 'turbopi_ros'
    filename = 'turbopi.urdf.xacro'

    pkg_path = os.path.join(get_package_share_directory(pkg_name))
    camera = eval(context.perform_substitution(LaunchConfiguration('camera')).title())
    camera_type = context.perform_substitution(LaunchConfiguration('camera_type'))
    drive = context.perform_substitution(LaunchConfiguration('drive'))
    lidar = eval(context.perform_substitution(LaunchConfiguration('lidar')).title())
    sim = eval(context.perform_substitution(LaunchConfiguration('sim')).title())
    camera_params_file = os.path.join(pkg_path, 'config', 'camera.yaml')
    slam_params_file = os.path.join(pkg_path, 'config', 'slam_toolbox.yaml')
    controller_params = os.path.join(pkg_path, 'config', 'turbopi_controllers.yaml')
    xacro_file = os.path.join(pkg_path,'description',filename)

    robot_description_content = Command(
        [
            PathJoinSubstitution([FindExecutable(name="xacro")]),
            " ",
            xacro_file,
            " ",
            "use_hardware:=",
            "mock" if sim else "robot",
            " ",
            "use_drive:=",
            drive,
            " ",
            "use_camera:=",
            camera_type,
            " ",
        ]
    )
    robot_description = {'robot_description': robot_description_content}

    controller_manager = Node(
        package='controller_manager',
        executable='ros2_control_node',
        parameters=[robot_description, controller_params],
        output='both',
    )

    node_robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        output='both',
        parameters=[robot_description],
    )

    diff_drive_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=[
            "diff_drive_controller", "-c", CM,
            "--controller-ros-args",
            "-r /diff_drive_controller/cmd_vel:=/cmd_vel",
            "--controller-ros-args",
            "-r /diff_drive_controller/odom:=/odom",
        ],
    )

    joint_broad_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster", "-c", CM],
    )

    mecanum_drive_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["mecanum_drive_controller", "-c", CM,
            "--controller-ros-args",
            "-r /mecanum_drive_controller/tf_odometry:=/tf",
            "--controller-ros-args",
            "-r /mecanum_drive_controller/reference:=/cmd_vel",
        ],
    )

    if drive == "mecanum":
        if _mecanum_drive_available():
            drive_spawner = mecanum_drive_spawner
        else:
            import sys
            print(
                "\n[WARNING] drive:=mecanum requested but mecanum_drive_controller is NOT "
                "installed on this system (not available in your ros2_controllers build).\n"
                "          Falling back to diff_drive_controller.\n"
                "          To install: sudo apt install ros-jazzy-mecanum-drive-controller\n",
                file=sys.stderr,
            )
            drive_spawner = diff_drive_spawner
    else:
        drive_spawner = diff_drive_spawner

    position_spawner = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["position_controllers", "-c", CM],
    )

    slam_toolbox_node = Node(
        package='slam_toolbox',
        executable='async_slam_toolbox_node',
        parameters=[ slam_params_file, {'use_sim_time': True} ],
    )

    battery_monitor_node = Node(
        package='turbopi_ros',
        executable='battery_monitor_node',
        parameters=[],
    )

    battery_node = Node(
        package='turbopi_ros',
        executable='battery_node',
        parameters=[],
    )

    infrared_node = Node(
        package='turbopi_ros',
        executable='infrared_node',
        parameters=[],
    )

    sonar_node = Node(
        package='turbopi_ros',
        executable='sonar_node',
        parameters=[],
    )

    start_lidar = ExecuteProcess(
        cmd=[
            [
                FindExecutable(name="ros2"),
                " service call ",
                "/start_motor ",
                "std_srvs/srv/Empty",
            ]
        ],
        shell=True,
    )

    v4l2_camera_node = Node(
        package='v4l2_camera',
        executable='v4l2_camera_node',
        parameters=[camera_params_file],
        remappings=[('/image_raw', '/camera'),],
    )

    delayed_joint_broad_spawner = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=controller_manager,
            on_start=[joint_broad_spawner],
        )
    )

    delayed_drive_spawner = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=joint_broad_spawner,
            on_exit=[drive_spawner],
        )
    )

    delayed_position_spawner = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=joint_broad_spawner,
            on_exit=[position_spawner],
        )
    )

    delayed_slam_toolbox_node_spawner = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=joint_broad_spawner,
            on_exit=[start_lidar, slam_toolbox_node],
        )
    )

    delayed_infrared_node_spawner = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=joint_broad_spawner,
            on_start=[infrared_node],
        )
    )

    delayed_sonar_node_spawner = RegisterEventHandler(
        event_handler=OnProcessStart(
            target_action=joint_broad_spawner,
            on_start=[sonar_node],
        )
    )

    delayed_v4l2_camera_node = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=joint_broad_spawner,
            on_exit=[v4l2_camera_node],
        )
    )

    stop_lidar_on_shutdown = RegisterEventHandler(
        event_handler=OnProcessExit(
            target_action=slam_toolbox_node,
            on_exit=[
                LogInfo(msg='Stopping lidar'),
                OpaqueFunction(function=stop_lidar),
                LogInfo(msg='Stopped lidar'),
            ],
        )
    )

    nodes = [
        battery_monitor_node,
        battery_node,
        controller_manager,
        node_robot_state_publisher,
        delayed_joint_broad_spawner,
        delayed_drive_spawner,
        delayed_infrared_node_spawner,
        delayed_sonar_node_spawner,
    ]

    # position_controllers (pan/tilt) only exist when the default 2DOF camera is used.
    # With the depth camera (camera_type != 'default') those joints are absent and the
    # JointGroupPositionController spawner would fail.
    if camera_type == 'default':
        nodes += [delayed_position_spawner]

    if camera:
        nodes += [delayed_v4l2_camera_node]

    if lidar:
        nodes += [
            delayed_slam_toolbox_node_spawner,
            stop_lidar_on_shutdown,
        ]

    return nodes


def stop_lidar(context: LaunchContext):
    subprocess.run("ros2 service call /stop_motor std_srvs/srv/Empty", shell=True)


def generate_launch_description():
    # Declare arguments
    declared_arguments = []
    declared_arguments.append(
        DeclareLaunchArgument(
            "camera",
            default_value="False",
            description="Start with v4l2_camera node (requires v4l2_camera package)",
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "camera_type",
            default_value="depth",
            description="Camera type: 'default' (2DOF pan/tilt) or 'depth' (Orbbec Astra S). "
                        "Use 'default' to enable position_controllers for pan/tilt servos.",
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "drive",
            default_value="diff",
            description="Drive system diff or mecanum",
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "lidar",
            default_value="False",
            description="Start with lidar hardware",
        )
    )
    declared_arguments.append(
        DeclareLaunchArgument(
            "sim",
            default_value="False",
            description="Start with simulated mock hardware",
        )
    )

    return LaunchDescription(declared_arguments + [OpaqueFunction(function=launch_setup)])
