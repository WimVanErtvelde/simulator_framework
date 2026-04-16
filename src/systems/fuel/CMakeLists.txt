cmake_minimum_required(VERSION 3.8)
project(sim_fuel)

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

# ─── Fuel Graph Solver Library ──────────────────────────────────────
# Standalone library — no ROS2 dependency
add_library(fuel_graph_solver SHARED
  src/fuel_graph_solver.cpp
)
target_include_directories(fuel_graph_solver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fuel_graph_solver
  yaml-cpp
)

# ─── Fuel Graph Solver Tests (generic only — aircraft tests in aircraft packages) ──
add_executable(test_fuel_graph_solver test/test_fuel_graph_solver.cpp)
target_link_libraries(test_fuel_graph_solver fuel_graph_solver)

# ─── ROS2 Node ───────────────────────────────────────────────────────
add_executable(fuel_node src/fuel_node.cpp)
ament_target_dependencies(fuel_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs pluginlib sim_interfaces ament_index_cpp)
target_link_libraries(fuel_node yaml-cpp)

# ─── Install ─────────────────────────────────────────────────────────
install(TARGETS fuel_graph_solver
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(TARGETS fuel_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_export_libraries(fuel_graph_solver)
ament_export_dependencies(yaml-cpp)

ament_package()
