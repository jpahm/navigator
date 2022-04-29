from os import name, path, environ

from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.actions import IncludeLaunchDescription
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.actions import ExecuteProcess
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

from ament_index_python import get_package_share_directory

def generate_launch_description():

    launch_path = path.realpath(__file__)
    launch_dir = path.join(path.dirname(launch_path), '..')
    param_dir = path.join(launch_dir,"param")

    serial = Node(
       package = 'serial',
       executable = 'serial',
       parameters = [
           (path.join(param_dir, "interface", "serial.param.yaml"))],
       remappings = [
           ("serial_incoming_lines", "serial_incoming_lines"),
           ("serial_outgoing_lines", "serial_outgoing_lines")])
    
    servo_throttle = Node(
        package = 'servo',
        executable = 'servo',
        parameters = [
            (path.join(param_dir, "interface", "servo_throttle.param.yaml"))],
        remappings = [
            ("servo_commands", "serial_outgoing_lines"),
            ("servo_positions", "throttle_position")])

    servo_brake = Node(
        package = 'servo',
        executable = 'servo',
        parameters = [
            (path.join(param_dir, "interface", "servo_brake.param.yaml"))],
        remappings = [
            ("servo_commands", "serial_outgoing_lines"),
            ('servo_positions', 'brake_position')])

    epas_can = Node(
        package = 'can_interface',
        executable = 'interface',
        remappings = [
            ('can_interface_incoming_can_frames', 'epas_incoming_can'),
            ('can_interface_outgoing_can_frames', 'epas_outgoing_can')],
        arguments = ['can0'])

    
    epas_reporter = Node(
        package = 'epas_translator',
        executable = 'reporter',
        parameters = [
            (path.join(param_dir,"interface","epas_reporter.param.yaml"))],
        remappings = [
            ("epas_translator_incoming_can_frames", "epas_incoming_can"),
            ("epas_translator_real_steering_angle", "real_steering_angle")])

    epas_controller = Node(
        package = 'epas_translator',
        executable = 'controller',
        remappings = [
            ("epas_translator_steering_power", "steering_power"),
            ("epas_translator_outgoing_can_frames", "epas_outgoing_can"),
            ("epas_translator_enable", "steering_enable")])

    steering_pid = Node(
        package = 'pid_controller',
        executable = 'pid_controller',
        parameters = [
            (path.join
             (param_dir,"interface","steering_pid_controller.param.yaml"))],
        remappings = [
            ("output", "steering_power"),
            ("target", "steering_target"),
            ("measurement", "real_steering_angle")])
            
    zed_interface = Node (
        package = 'zed_interface',
        executable = 'zed_interface_exe'
    )
    gnss_parser = Node(
        package = 'gnss_parser',
        executable = 'gnss_parser',
        remappings = [
            ("/sensors/gnss/odom", "/sensors/gnss/odom"),
            ("/serial/gnss", "serial_incoming_lines")])

    
    vehicle_can = Node(
        package = 'can_interface',
        executable = 'interface',
        remappings = [
            ('can_interface_incoming_can_frames', 'vehicle_incoming_can'),
            ('can_interface_outgoing_can_frames', 'vehicle_outgoing_can')],
        arguments = ['can1'])

    
    speedometer_reporter = Node(
        package='can_translation',
        executable='float_reporter',
        parameters=[(path.join
                     (param_dir,"interface","speedometer_reporter.param.yaml"))],
        remappings=[
            ("incoming_can_frames", "vehicle_incoming_can"),
            ("result_topic", "vehicle_speedometer")])

    speedometer_translator = Node(
        package='msg_translation',
        executable='velocity_to_twist',
        remappings=[
            ("velocity_topic", "vehicle_speedometer"),
            ("twist_topic", "speedometer_odom")])


    path_publisher = Node(
        package='path_publisher',
        executable='publisher',
        parameters=[
            (path.join(param_dir,"planning","path_publisher.param.yaml"))],
        remappings=[
            ("paths", "paths"),
            ("path_pub_viz", "path_pub_viz")])

    unified_controller = Node(
        package='unified_controller',
        executable='unified_controller_node',
        remappings=[
            ("/planning/outgoing_trajectory", "trajectory"),
            ("/gnss_odom", "/sensors/gnss/odom"),
            ("/command/throttle_position", "throttle_position"),
            ("/command/brake_position", "brake_position"),
            ("/command/steering_position", "steering_target")])

    motion_planner = Node(
        package='motion_planner',
        executable='motion_planner',
        remappings=[
            ("outgoing_trajectory", "trajectory"),
            ("planning/paths", "paths"),
            ("/planning/zones", "zones"),
            ("/gnss_odom", "/sensors/gnss/odom")],
        parameters=[
            (path.join(param_dir,"planning","motion_planner.param.yaml"))])

    zone_fusion = Node(
        package='zone_fusion',
        executable='ZoneFusionLaunch',
        remappings=[
            ("/planning/zone_array", "traffic_zones"),
            ("/planning/obstacle_zone_array", "obstacle_zones"),
            ("zones", "zones")])

    obstacle_zoner = Node(
        package='obstacle_zoner',
        executable='ObstacleZonerLaunch',
        remappings=[
            ("obstacle_zone_array", "obstacle_zones"),
            ("/sensors/zed/obstacle_array_3d", "zed_obstacles")])

    behavior_planner = Node(
        package='nova_behavior_planner',
        executable='BehaviorPlannerLaunch',
        parameters=[
            (path.join(param_dir,"planning","path_publisher.param.yaml"))],
        remappings=[
            ("/sensors/gnss/odom", "odometry"),
            ("paths", "paths"),
            ("/sensors/zed/obstacle_array_3d", "zed_obstacles")])

    """""""""
    " LIDAR "
    """""""""
    lidar_driver_front = Node(
        package='velodyne_driver',
        executable='velodyne_driver_node',
        namespace='lidar_front',
        parameters=[(path.join(launch_dir, "param", "perception","lidar_driver_front.param.yaml"))]
    )
    lidar_pointcloud_front = Node(
        package='velodyne_pointcloud',
        executable='velodyne_convert_node',
        namespace='lidar_front',
        parameters=[(path.join(launch_dir, "param", "perception","lidar_pointcloud_front.param.yaml"))]
    )
    lidar_driver_rear = Node(
        package='velodyne_driver',
        executable='velodyne_driver_node',
        namespace='lidar_rear',
        parameters=[(path.join(launch_dir, "param", "perception","lidar_driver_rear.param.yaml"))]
    )
    lidar_pointcloud_rear = Node(
        package='velodyne_pointcloud',
        executable='velodyne_convert_node',
        namespace='lidar_rear',
        parameters=[(path.join(launch_dir, "param", "perception","lidar_pointcloud_rear.param.yaml"))]
    )

    lidar_fusion = Node(
        package='lidar_fusion',
        name='lidar_fusion_node',
        executable='lidar_fusion_node',
        remappings=[
            ('/lidar_front', '/lidar_front/velodyne_points'),
            ('/lidar_rear', '/lidar_rear/velodyne_points'),
            ('/lidar_fused', '/lidar_fused')
        ]
    )

    urdf_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        arguments=[path.join(launch_dir, "data", "hail_bopp.urdf")]
    )

    odr_viz = Node(
        package='odr_visualizer',
        executable='visualizer',
        parameters=[
            (path.join(param_dir,"mapping","odr.param.yaml"))
        ],
        output='screen',
        remappings=[
            ("/map","/odr_map")
        ]
    )

    odom_bl_link = Node(
        package='tf2_ros',
        executable='static_transform_publisher',
        arguments=[
            '0.0','0.0','0.0','0.0','0.0','0.0','1.0','odom','base_link'
        ]
    )

    map_odom_ukf = Node(
        package='robot_localization',
        executable='ukf_node',
        name='localization_map_odom',
        parameters=[path.join(param_dir, "atlas", "map_odom.param.yaml")],
        remappings=[
            ("/odom0", "/gnss_odom"),
            ("/imu0", "/sensors/zed/imu")
        ]
    )

    pcl_localization = Node(
        package='pcl_localization_ros2',
        executable='pcl_localization_node',
        remappings=[
            #inputs
            ('/cloud', '/lidar_fused'),
            #('/odom', '/sensors/gnss/odom'),
            ('/imu', '/sensors/zed/imu'),
            ('/initialpose', '/sensors/gnss/odom'),
            #output
            ('/pcl_pose', '/pose/ndt'),
        ],
        #keeping default parameter path for now
        #it broke when I moved it for some reason
        parameters=[path.join(launch_dir, "src", "atlas", "pcl_localization_ros2", "param", "localization.yaml")]
    )

    landmark_localizer = Node(
        package = 'landmark_localizer',
        executable = 'landmark_localizer_node'
    )
    # MISSING PIECES:
    # obstacle detection
    # base link transform?
    # visualization

    lidar_obstacle_detector = Node(
        package='lidar_obstacle_detector',
        name='lidar_obstacle_detector_node',
        executable='lidar_obstacle_detector_node_exe',
        remappings=[
            ("lidar_points", "/lidar_fused"),
            ("/zones", "/planning/zones"),
        ],
        parameters=[(path.join(param_dir,"perception","lidar_obstacle_detector_node.param.yaml"))],
    )

    visuals = Node(
        package='vt_viz',
        name='vt_viz_node',
        executable='vt_viz_exe',
    )

    curb_detector = Node(
        package='curb_detection',
        executable='curb_detector',
        remappings=[
        ("input_points", "/lidar_fused"),
        ]
    )

    curb_localizer = Node(
        package='curb_localizer',
        executable='curb_localizer_exe'
    )

    
    return LaunchDescription([
        # PERCEPTION
        lidar_fusion,
        # lidar_obstacle_detector,

        # # HARDWARE
        # serial,
        # servo_brake,
        # servo_throttle,

        # # Steering
        # epas_can,
        # epas_reporter,
        # epas_controller,
        # steering_pid,
        # lidar_driver_front,
        # lidar_pointcloud_front,
        # lidar_driver_rear,
        # lidar_pointcloud_rear,

        # # Camera
        # zed_interface,
        # gnss_parser,
        # vehicle_can,
        # speedometer_reporter,
        # speedometer_translator,

        # BEHAVIOR
        path_publisher,
        motion_planner,
        # zone_fusion,
        # obstacle_zoner,
        # behavior_planner,

        # STATE ESTIMATION
        map_odom_ukf,
        # pcl_localization,
        curb_detector,
        curb_localizer,

        # CONTROL
        unified_controller,

        # MISC
        # landmark_localizer
        odr_viz,
        odom_bl_link,
        urdf_publisher,
        # visuals
    ])
