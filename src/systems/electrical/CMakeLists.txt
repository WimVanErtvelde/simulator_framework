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

# nlohmann/json (header-only, used by solver JSON loader)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ─── Solver Library ──────────────────────────────────────────────
# Standalone library — no ROS2 dependency, usable by aircraft plugins
add_library(electrical_solver SHARED
  src/electrical_solver.cpp
)
target_include_directories(electrical_solver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(electrical_solver
  nlohmann_json::nlohmann_json
  yaml-cpp
)

# ─── ROS2 Node ───────────────────────────────────────────────────
add_executable(electrical_node src/electrical_node.cpp)
ament_target_dependencies(electrical_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs pluginlib sim_interfaces ament_index_cpp)

# ─── Install ─────────────────────────────────────────────────────
install(TARGETS electrical_solver
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
ament_export_libraries(electrical_solver)
ament_export_dependencies(yaml-cpp)

ament_package()
