cmake_minimum_required(VERSION 3.8)
project(flight_model_adapter)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

# --- JSBSim via FetchContent ---
include(FetchContent)
FetchContent_Declare(
  jsbsim
  GIT_REPOSITORY https://github.com/JSBSim-Team/jsbsim.git
  GIT_TAG        v1.2.1
  GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(JSBSIM_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_JSBSIM_CONTENT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jsbsim)

FetchContent_GetProperties(jsbsim SOURCE_DIR JSBSIM_SOURCE_DIR)

# --- Adapter library (links JSBSim + sim_msgs) ---
add_library(jsbsim_adapter STATIC
  src/JSBSimAdapter.cpp
  src/jsbsim/JSBSimElectricalWriteback.cpp
  src/jsbsim/JSBSimFuelWriteback.cpp)
target_include_directories(jsbsim_adapter PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(jsbsim_adapter SYSTEM PRIVATE
  ${JSBSIM_SOURCE_DIR}/src)
target_link_libraries(jsbsim_adapter PUBLIC
  libJSBSim
  ${sim_msgs_TARGETS})

# --- Node executable ---
add_executable(flight_model_adapter_node src/flight_model_adapter_node.cpp)
target_include_directories(flight_model_adapter_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(flight_model_adapter_node PRIVATE
  JSBSIM_ROOT_DIR="${JSBSIM_SOURCE_DIR}")
target_link_libraries(flight_model_adapter_node PRIVATE
  jsbsim_adapter
  ${rclcpp_TARGETS}
  ${rclcpp_lifecycle_TARGETS}
  ${sim_msgs_TARGETS}
  ${std_msgs_TARGETS})

install(TARGETS flight_model_adapter_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()
