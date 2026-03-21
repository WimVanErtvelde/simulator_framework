cmake_minimum_required(VERSION 3.8)
project(aircraft_ec135)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sim_interfaces REQUIRED)
find_package(sim_electrical REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(aircraft_ec135_plugins SHARED
  src/electrical_model.cpp
  src/fuel_model.cpp
  src/engines_model.cpp
  src/gear_model.cpp
  src/navigation_model.cpp)
target_include_directories(aircraft_ec135_plugins PRIVATE
  ${sim_interfaces_INCLUDE_DIRS}
  ${sim_electrical_INCLUDE_DIRS})
target_link_libraries(aircraft_ec135_plugins
  ${sim_electrical_LIBRARIES}
  yaml-cpp)
ament_target_dependencies(aircraft_ec135_plugins pluginlib sim_interfaces sim_electrical sim_msgs)

pluginlib_export_plugin_description_file(sim_interfaces plugins.xml)

install(TARGETS aircraft_ec135_plugins
  DESTINATION lib)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

ament_package()
