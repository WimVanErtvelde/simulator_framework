#!/bin/bash
source /opt/ros/jazzy/setup.bash
source ~/simulator_framework/install/setup.bash
QT_SCALE_FACTOR=1.5 ros2 run plotjuggler plotjuggler --buffer_size 120
