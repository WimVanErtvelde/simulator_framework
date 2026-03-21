cmake_minimum_required(VERSION 3.8)
project(navaid_sim)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(lifecycle_msgs REQUIRED)

# Navaid sim core library (all the navigation logic, no ROS2 dependency)
set(NAVAID_SIM_CORE_SOURCES
  src/A424Parser.cpp
  src/AbstractReceiver.cpp
  src/DME_Receiver.cpp
  src/ILS_GlideslopeReceiver.cpp
  src/ILS_LocalizerReceiver.cpp
  src/ILS_MarkerReceiver.cpp
  src/LOSChecker.cpp
  src/LatLon.cpp
  src/MagDec.cpp
  src/Model.cpp
  src/NDB_Receiver.cpp
  src/NavSimTask.cpp
  src/TerrainModel.cpp
  src/TileGrid.cpp
  src/Units.cpp
  src/VOR_Receiver.cpp
  src/World.cpp
  src/WorldParser.cpp
  src/AirportDatabase.cpp
)

add_library(navaid_sim_core STATIC ${NAVAID_SIM_CORE_SOURCES})
target_include_directories(navaid_sim_core PUBLIC src)
target_compile_features(navaid_sim_core PUBLIC cxx_std_17)

# ROS2 lifecycle node
add_executable(navaid_sim_node src/navaid_sim_node.cpp)
target_link_libraries(navaid_sim_node navaid_sim_core)
ament_target_dependencies(navaid_sim_node
  rclcpp rclcpp_lifecycle sim_msgs std_msgs lifecycle_msgs)

install(TARGETS navaid_sim_node
  DESTINATION lib/${PROJECT_NAME})

# Install data files so they can be found at runtime
install(DIRECTORY data/
  DESTINATION share/${PROJECT_NAME}/data
  PATTERN "*.obj" EXCLUDE
  PATTERN "*.exe" EXCLUDE)

ament_package()
