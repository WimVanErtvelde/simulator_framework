cmake_minimum_required(VERSION 3.8)
project(sim_pressurization)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(pressurization_node src/pressurization_node.cpp)
ament_target_dependencies(pressurization_node rclcpp rclcpp_lifecycle sim_msgs std_msgs)

install(TARGETS pressurization_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
