cmake_minimum_required(VERSION 3.8)
project(sim_ice_protection)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(ice_protection_node src/ice_protection_node.cpp)
ament_target_dependencies(ice_protection_node rclcpp rclcpp_lifecycle sim_msgs std_msgs)

install(TARGETS ice_protection_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
