cmake_minimum_required(VERSION 3.8)
project(sim_engine_systems)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sim_interfaces REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(engines_node src/engines_node.cpp)
ament_target_dependencies(engines_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs pluginlib sim_interfaces ament_index_cpp)
target_link_libraries(engines_node yaml-cpp)

install(TARGETS engines_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
