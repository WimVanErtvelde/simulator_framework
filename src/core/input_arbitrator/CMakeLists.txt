cmake_minimum_required(VERSION 3.8)
project(input_arbitrator)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(input_arbitrator_node src/input_arbitrator_node.cpp)
ament_target_dependencies(input_arbitrator_node rclcpp rclcpp_lifecycle sim_msgs std_msgs)

install(TARGETS input_arbitrator_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
