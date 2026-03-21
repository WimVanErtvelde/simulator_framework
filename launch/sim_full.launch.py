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
        'aircraft_config_dir', default_value='src/aircraft',
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
             name='flight_model_adapter', parameters=[sim_time, {
                 'aircraft_id': aircraft_id,
                 'fdm_type': 'jsbsim',
             }]),
        Node(package='input_arbitrator', executable='input_arbitrator_node',
             name='input_arbitrator', parameters=[sim_time]),
        Node(package='atmosphere_node', executable='atmosphere_node',
             name='atmosphere_node', parameters=[sim_time]),
        Node(package='sim_cigi_bridge', executable='cigi_bridge_node',
             name='cigi_bridge', parameters=[sim_time]),

        # Systems with pluginlib (lifecycle nodes — load aircraft-specific plugins)
        Node(package='sim_electrical', executable='electrical_node',
             name='sim_electrical', parameters=[sim_time, {'aircraft_id': aircraft_id}]),
        Node(package='sim_fuel', executable='fuel_node',
             name='sim_fuel', parameters=[sim_time, {'aircraft_id': aircraft_id}]),
        Node(package='sim_engine_systems', executable='engines_node',
             name='sim_engine_systems', parameters=[sim_time, {'aircraft_id': aircraft_id}]),
        Node(package='sim_gear', executable='gear_node',
             name='sim_gear', parameters=[sim_time, {'aircraft_id': aircraft_id}]),
        Node(package='sim_navigation', executable='navigation_node',
             name='sim_navigation', parameters=[sim_time]),

        # Systems without pluginlib (lifecycle nodes — no aircraft-specific model yet)
        Node(package='sim_hydraulic', executable='hydraulic_node',
             name='sim_hydraulic', parameters=[sim_time]),
        Node(package='sim_failures', executable='failures_node',
             name='sim_failures', parameters=[sim_time, {'aircraft_id': aircraft_id}]),
        Node(package='sim_ice_protection', executable='ice_protection_node',
             name='sim_ice_protection', parameters=[sim_time]),
        Node(package='sim_pressurization', executable='pressurization_node',
             name='sim_pressurization', parameters=[sim_time]),

        # Radio navigation
        Node(package='navaid_sim', executable='navaid_sim_node',
             name='navaid_sim', parameters=[sim_time, {
                 'navdb_format': 'a424',
                 'navdb_path': 'src/core/navaid_sim/data/euramec.pc',
                 'terrain_dir': 'src/core/navaid_sim/data/srtm3/',
                 'airport_db_format': 'xp12',
                 'airport_db_path': 'src/core/navaid_sim/data/apt.dat',
             }]),

        # Hardware
        Node(package='microros_bridge', executable='microros_bridge_node',
             name='microros_bridge', parameters=[sim_time]),

        # Python nodes
        Node(package='qtg_engine', executable='qtg_engine_node',
             name='qtg_engine', parameters=[sim_time]),
    ])
