cmake_minimum_required(VERSION 3.8)
project(aircraft_c172)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sim_interfaces REQUIRED)
find_package(sim_electrical REQUIRED)
find_package(sim_fuel REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(aircraft_c172_plugins SHARED
  src/electrical_model.cpp
  src/fuel_model.cpp
  src/engines_model.cpp
  src/gear_model.cpp
  src/navigation_model.cpp
  src/air_data_model.cpp)
target_include_directories(aircraft_c172_plugins PRIVATE
  ${sim_interfaces_INCLUDE_DIRS}
  ${sim_electrical_INCLUDE_DIRS}
  ${sim_fuel_INCLUDE_DIRS})
target_link_libraries(aircraft_c172_plugins
  ${sim_electrical_LIBRARIES}
  ${sim_fuel_LIBRARIES}
  yaml-cpp)
ament_target_dependencies(aircraft_c172_plugins pluginlib sim_interfaces sim_electrical sim_fuel sim_msgs)

pluginlib_export_plugin_description_file(sim_interfaces plugins.xml)

install(TARGETS aircraft_c172_plugins
  DESTINATION lib)

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

ament_package()
