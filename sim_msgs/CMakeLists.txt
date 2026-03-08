cmake_minimum_required(VERSION 3.8)
project(sim_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/RawFlightControls.msg"
  "msg/RawEngineControls.msg"
  "msg/RawAvionicsControls.msg"
  "msg/DeviceHeartbeat.msg"
  "msg/SimState.msg"
  "msg/FdmState.msg"
  "msg/FlightControls.msg"
  "msg/EngineControls.msg"
  "msg/AvionicsControls.msg"
  "msg/ArbitrationState.msg"
  "msg/ElectricalState.msg"
  "msg/FuelState.msg"
  "msg/HydraulicState.msg"
  "msg/NavState.msg"
  "msg/EngineState.msg"
  "msg/FailureList.msg"
  "msg/WeatherState.msg"
  "msg/CigiPacket.msg"
  DEPENDENCIES builtin_interfaces
)

ament_package()
