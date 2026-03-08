cmake_minimum_required(VERSION 3.8)
project(atmosphere_node)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sim_msgs REQUIRED)

add_executable(atmosphere_node src/atmosphere_node.cpp)
ament_target_dependencies(atmosphere_node rclcpp sim_msgs)

install(TARGETS atmosphere_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
