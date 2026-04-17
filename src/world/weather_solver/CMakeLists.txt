cmake_minimum_required(VERSION 3.8)
project(weather_solver)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

# ── Standalone libraries (no rclcpp) ─────────────────────────────────────────
add_library(dryden_turbulence STATIC src/dryden_turbulence.cpp)
target_include_directories(dryden_turbulence PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_library(microburst_model STATIC src/microburst_model.cpp)
target_include_directories(microburst_model PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

add_library(weather_solver_lib STATIC src/weather_solver.cpp)
target_include_directories(weather_solver_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(weather_solver_lib PUBLIC dryden_turbulence microburst_model)
ament_target_dependencies(weather_solver_lib PUBLIC sim_msgs)

# ── ROS2 node executable ────────────────────────────────────────────────────
add_executable(weather_solver_node src/weather_solver_node.cpp)
target_include_directories(weather_solver_node PRIVATE include)
target_link_libraries(weather_solver_node weather_solver_lib)
ament_target_dependencies(weather_solver_node
  rclcpp rclcpp_lifecycle lifecycle_msgs sim_msgs std_msgs)

install(TARGETS weather_solver_node
  DESTINATION lib/${PROJECT_NAME})

# ── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_dryden_turbulence test/test_dryden_turbulence.cpp)
  target_link_libraries(test_dryden_turbulence dryden_turbulence)
  target_include_directories(test_dryden_turbulence PRIVATE include)

  ament_add_gtest(test_weather_solver test/test_weather_solver.cpp)
  target_link_libraries(test_weather_solver weather_solver_lib)
  target_include_directories(test_weather_solver PRIVATE include)

  ament_add_gtest(test_microburst_model test/test_microburst_model.cpp)
  target_link_libraries(test_microburst_model microburst_model)
  target_include_directories(test_microburst_model PRIVATE include)
endif()

ament_package()
