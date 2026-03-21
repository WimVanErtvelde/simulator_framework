cmake_minimum_required(VERSION 3.8)
project(sim_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/RawFlightControls.msg"
  "msg/RawEngineControls.msg"
  "msg/RawAvionicsControls.msg"
  "msg/DeviceHeartbeat.msg"
  "msg/SimState.msg"
  "msg/FlightModelState.msg"
  "msg/FlightModelCapabilities.msg"
  "msg/FlightControls.msg"
  "msg/EngineControls.msg"
  "msg/AvionicsControls.msg"
  "msg/ArbitrationState.msg"
  "msg/ElectricalState.msg"
  "msg/FuelState.msg"
  "msg/GearState.msg"
  "msg/HydraulicState.msg"
  "msg/NavigationState.msg"
  "msg/EngineState.msg"
  "msg/EngineCommands.msg"
  "msg/FailureList.msg"
  "msg/WeatherState.msg"
  "msg/CigiPacket.msg"
  "msg/AtmosphereState.msg"
  "msg/NavSignalTable.msg"
  "msg/TerrainState.msg"
  "msg/ControlsState.msg"
  "msg/FailureState.msg"
  "msg/FailureCommand.msg"
  "msg/FailureInjection.msg"
  "msg/SimCommand.msg"
  "msg/SimAlert.msg"
  "msg/InitialConditions.msg"
  "msg/PanelControls.msg"
  "msg/ScenarioEvent.msg"
  "msg/HatHotResponse.msg"
  "msg/TerrainSource.msg"
  "msg/HotRequest.msg"
  "msg/Runway.msg"
  "msg/Airport.msg"
  "srv/GetTerrainElevation.srv"
  "srv/SearchAirports.srv"
  "srv/GetRunways.srv"
  DEPENDENCIES builtin_interfaces std_msgs
)

ament_package()
