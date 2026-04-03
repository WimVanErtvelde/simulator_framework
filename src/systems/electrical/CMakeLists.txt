cmake_minimum_required(VERSION 3.8)
project(sim_electrical)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sim_interfaces REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(yaml-cpp REQUIRED)

# ─── Graph Solver Library ────────────────────────────────────────
# Standalone library — no ROS2 dependency
add_library(graph_solver SHARED
  src/graph_solver.cpp
)
target_include_directories(graph_solver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(graph_solver
  yaml-cpp
)

# ─── Graph Solver Tests ─────────────────────────────────────────
add_executable(test_graph_solver test/test_graph_solver.cpp)
target_link_libraries(test_graph_solver graph_solver)
target_compile_definitions(test_graph_solver PRIVATE
  TEST_YAML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../aircraft/c172/config/electrical.yaml"
)

# ─── ROS2 Node ───────────────────────────────────────────────────
add_executable(electrical_node src/electrical_node.cpp)
ament_target_dependencies(electrical_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs pluginlib sim_interfaces ament_index_cpp)

# ─── Install ─────────────────────────────────────────────────────
install(TARGETS graph_solver
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(TARGETS electrical_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_export_libraries(graph_solver)
ament_export_dependencies(yaml-cpp)

ament_package()
