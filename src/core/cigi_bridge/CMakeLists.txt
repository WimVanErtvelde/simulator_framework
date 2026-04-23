cmake_minimum_required(VERSION 3.8)
project(sim_cigi_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── ROS2 dependencies ────────────────────────────────────────────────────────
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(lifecycle_msgs REQUIRED)
find_package(sim_msgs REQUIRED)
find_package(std_msgs REQUIRED)

# ── CIGI Class Library (CCL) — required for the host node now that datagram
# assembly/parsing goes through cigi_session → CigiHostSession.
# ─────────────────────────────────────────────────────────────────────────────
find_path(CCL_INCLUDE_DIR
  NAMES CigiIGSession.h
  PATHS
    /usr/include/cigicl
    /usr/local/include/cigicl
    /usr/local/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../references/CIGI/cigi3.3/ccl/install/include/cigicl
    ${CCL_ROOT}/include/cigicl
    ${CMAKE_CURRENT_SOURCE_DIR}/cigi_ig_interface
  NO_DEFAULT_PATH
)

# Prefer the static archive so the cigi_bridge_node has no runtime
# dependency on libcigicl.so (which lives outside any system path).
set(_ccl_save_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
find_library(CCL_LIBRARY
  NAMES cigicl CigiCCL
  PATHS
    /usr/lib
    /usr/local/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../references/CIGI/cigi3.3/ccl/install/lib
    ${CCL_ROOT}/lib
)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_ccl_save_suffixes})

if(NOT CCL_INCLUDE_DIR OR NOT CCL_LIBRARY)
  message(FATAL_ERROR
    "CCL (CigiIGSession.h / libcigicl) not found.\n"
    "  Build it from references/CIGI/cigi3.3/ccl/:\n"
    "    cd references/CIGI/cigi3.3/ccl && cmake -B build && cmake --build build\n"
    "    cmake --install build --prefix \"$(pwd)/install\"\n"
    "  Or set -DCCL_ROOT=<prefix>.")
endif()

message(STATUS "CCL found at ${CCL_INCLUDE_DIR}")

add_subdirectory(cigi_session)

# ── Host-side node (datagram assembly via cigi_session → CCL) ───────────────
add_executable(cigi_bridge_node
  src/cigi_host_node.cpp
  src/hat_request_tracker.cpp
  src/weather_sync.cpp
)

target_include_directories(cigi_bridge_node PRIVATE
  include
)

target_link_libraries(cigi_bridge_node cigi_session)

ament_target_dependencies(cigi_bridge_node
  rclcpp
  rclcpp_lifecycle
  lifecycle_msgs
  sim_msgs
  std_msgs
)

install(TARGETS cigi_bridge_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config/
  DESTINATION share/${PROJECT_NAME}/config)

# ── Vendored CIGI_IG_Interface (legacy example IG-side sources) ─────────────
# Contains TinyXML with pre-existing missing <cstring> includes; off by
# default. Set -DBUILD_CIGI_IG_INTERFACE=ON to enable.
option(BUILD_CIGI_IG_INTERFACE "Build the vendored CIGI_IG_Interface example library" OFF)
if(BUILD_CIGI_IG_INTERFACE)
  add_library(cigi_ig_interface STATIC
    cigi_ig_interface/CIGI_IG_Interface.cpp
    cigi_ig_interface/DefaultProc.cpp
    cigi_ig_interface/EntityCtrl.cpp
    cigi_ig_interface/HatHotReq.cpp
    cigi_ig_interface/IGCtrl.cpp
    cigi_ig_interface/WeatherReq.cpp
    cigi_ig_interface/ShortCompCtrl.cpp
    cigi_ig_interface/Network.cpp
    cigi_ig_interface/tinyxml.cpp
    cigi_ig_interface/tinyxmlerror.cpp
    cigi_ig_interface/tinyxmlparser.cpp
  )

  target_include_directories(cigi_ig_interface PUBLIC
    cigi_ig_interface
    ${CCL_INCLUDE_DIR}
  )

  target_compile_definitions(cigi_ig_interface PUBLIC TIXML_USE_STL)
  target_link_libraries(cigi_ig_interface PUBLIC ${CCL_LIBRARY})
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  # Host-node unit tests retired: weather_encoder is replaced by direct
  # HostSession emission (covered by cigi_session's own round-trip tests),
  # and the wire-conformance tests rode on the hand-rolled encoder. Only
  # tests that live here now are under cigi_session/test/.
endif()

ament_package()
