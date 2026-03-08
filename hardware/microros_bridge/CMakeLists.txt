cmake_minimum_required(VERSION 3.8)
project(microros_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sim_msgs REQUIRED)

add_executable(microros_bridge_node src/microros_bridge_node.cpp)
ament_target_dependencies(microros_bridge_node rclcpp sim_msgs)

install(TARGETS microros_bridge_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
