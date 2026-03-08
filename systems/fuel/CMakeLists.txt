cmake_minimum_required(VERSION 3.8)
project(sim_fuel)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(fuel_node src/fuel_node.cpp)
ament_target_dependencies(fuel_node rclcpp sim_msgs std_msgs)

install(TARGETS fuel_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
