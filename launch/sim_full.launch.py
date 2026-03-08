"""Full simulator launch — starts sim_manager and all system nodes."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    aircraft_id_arg = DeclareLaunchArgument(
        'aircraft_id', default_value='c172',
        description='Aircraft type to load (must match aircraft/<id>/config.yaml)')

    aircraft_config_dir_arg = DeclareLaunchArgument(
        'aircraft_config_dir', default_value='aircraft',
        description='Path to aircraft config directory')

    sim_time = {'use_sim_time': True}
    aircraft_id = LaunchConfiguration('aircraft_id')
    config_dir = LaunchConfiguration('aircraft_config_dir')

    return LaunchDescription([
        aircraft_id_arg,
        aircraft_config_dir_arg,

        # Sim Manager — drives /clock, does NOT use sim time itself
        Node(package='sim_manager', executable='sim_manager_node',
             name='sim_manager', parameters=[{
                 'use_sim_time': False,
                 'aircraft_id': aircraft_id,
                 'aircraft_config_dir': config_dir,
             }]),

        # Core
        Node(package='flight_model_adapter', executable='flight_model_adapter_node',
             name='flight_model_adapter', parameters=[sim_time]),
        Node(package='input_arbitrator', executable='input_arbitrator_node',
             name='input_arbitrator', parameters=[sim_time]),
        Node(package='atmosphere_node', executable='atmosphere_node',
             name='atmosphere_node', parameters=[sim_time]),
        Node(package='cigi_bridge', executable='cigi_bridge_node',
             name='cigi_bridge', parameters=[sim_time]),

        # Systems
        Node(package='sim_electrical', executable='electrical_node',
             name='electrical_node', parameters=[sim_time]),
        Node(package='sim_fuel', executable='fuel_node',
             name='fuel_node', parameters=[sim_time]),
        Node(package='sim_hydraulic', executable='hydraulic_node',
             name='hydraulic_node', parameters=[sim_time]),
        Node(package='sim_navigation', executable='navigation_node',
             name='navigation_node', parameters=[sim_time]),
        Node(package='sim_engines', executable='engines_node',
             name='engines_node', parameters=[sim_time]),
        Node(package='sim_failures', executable='failures_node',
             name='failures_node', parameters=[sim_time]),
        Node(package='sim_ice_protection', executable='ice_protection_node',
             name='ice_protection_node', parameters=[sim_time]),
        Node(package='sim_pressurization', executable='pressurization_node',
             name='pressurization_node', parameters=[sim_time]),
        Node(package='sim_gear', executable='gear_node',
             name='gear_node', parameters=[sim_time]),

        # Hardware
        Node(package='microros_bridge', executable='microros_bridge_node',
             name='microros_bridge', parameters=[sim_time]),

        # Python nodes
        Node(package='ios_backend', executable='ios_backend_node',
             name='ios_backend', parameters=[sim_time]),
        Node(package='qtg_engine', executable='qtg_engine_node',
             name='qtg_engine', parameters=[sim_time]),
    ])
