cmake_minimum_required(VERSION 3.8)
project(sim_navigation)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(lifecycle_msgs REQUIRED)

add_executable(navigation_node src/navigation_node.cpp)
ament_target_dependencies(navigation_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs lifecycle_msgs)

install(TARGETS navigation_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
