cmake_minimum_required(VERSION 3.8)
project(sim_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(rosgraph_msgs REQUIRED)

add_executable(sim_manager_node src/sim_manager_node.cpp)
ament_target_dependencies(sim_manager_node rclcpp sim_msgs rosgraph_msgs)

install(TARGETS sim_manager_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
