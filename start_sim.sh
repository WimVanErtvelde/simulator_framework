#!/bin/bash
# Terminal 1 — Simulator core (launch all ROS2 nodes)
cd ~/simulator_framework
source /opt/ros/jazzy/setup.bash && source install/setup.bash
ros2 launch launch/sim_full.launch.py
