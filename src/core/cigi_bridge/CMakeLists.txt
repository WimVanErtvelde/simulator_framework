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

# ── Host-side node (raw CIGI 3.3 UDP, no CCL dependency) ────────────────────
add_executable(cigi_bridge_node
  src/cigi_host_node.cpp
  src/hat_request_tracker.cpp
  src/weather_encoder.cpp
  src/weather_sync.cpp
)

target_include_directories(cigi_bridge_node PRIVATE
  include
)

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

# ── Vendored CIGI_IG_Interface (IG-side sources for X-Plane plugin reference)
# These sources require the CIGI Class Library (CCL).  They are compiled into
# a static library only when CCL headers are found; otherwise they are skipped
# so the ROS2 package builds cleanly without CCL installed.
# To install CCL: build from source or provide headers at /usr/local/include.
# ─────────────────────────────────────────────────────────────────────────────
find_path(CCL_INCLUDE_DIR
  NAMES CigiIGSession.h
  PATHS
    /usr/include/cigicl
    /usr/local/include/cigicl
    /usr/local/include
    /tmp/cigi-install/include/cigicl
    ${CCL_ROOT}/include/cigicl
    ${CMAKE_CURRENT_SOURCE_DIR}/cigi_ig_interface
  NO_DEFAULT_PATH
)

if(CCL_INCLUDE_DIR)
  message(STATUS "CCL found at ${CCL_INCLUDE_DIR}")

  find_library(CCL_LIBRARY
    NAMES cigicl CigiCCL
    PATHS
      /usr/lib
      /usr/local/lib
      /tmp/cigi-install/lib
      ${CCL_ROOT}/lib
  )
endif()

# Vendored CIGI_IG_Interface — only build when explicitly requested. Contains
# example IG-side code with vendored TinyXML that has pre-existing missing
# <cstring> includes; off by default so the package builds cleanly with CCL
# present. Set -DBUILD_CIGI_IG_INTERFACE=ON to enable.
option(BUILD_CIGI_IG_INTERFACE "Build the vendored CIGI_IG_Interface example library" OFF)
if(CCL_INCLUDE_DIR AND BUILD_CIGI_IG_INTERFACE)
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

  if(CCL_LIBRARY)
    target_link_libraries(cigi_ig_interface PUBLIC ${CCL_LIBRARY})
  endif()
endif()

if(NOT CCL_INCLUDE_DIR)
  message(STATUS
    "CCL (CigiIGSession.h) not found — vendored CIGI_IG_Interface and"
    " test_cigi_wire_conformance skipped.\n"
    "  Install CCL headers to /usr/local/include (or set -DCCL_ROOT=) to enable.\n"
    "  The cigi_bridge_node still builds and sends raw CIGI 3.3 packets without CCL.")
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_weather_encoder
    test/test_weather_encoder.cpp
    src/weather_encoder.cpp
  )
  target_include_directories(test_weather_encoder PRIVATE include)
  ament_target_dependencies(test_weather_encoder rclcpp sim_msgs)

  ament_add_gtest(test_weather_sync
    test/test_weather_sync.cpp
    src/weather_sync.cpp
    src/weather_encoder.cpp
  )
  target_include_directories(test_weather_sync PRIVATE include)
  ament_target_dependencies(test_weather_sync rclcpp sim_msgs)

  # ── CIGI wire-format conformance test (round-trips against Boeing CCL) ───
  # Built only when CCL is available. Without CCL this skill-bundled test is
  # skipped — the host node still builds and runs.
  if(CCL_INCLUDE_DIR AND CCL_LIBRARY)
    message(STATUS "CCL found — building test_cigi_wire_conformance")
    ament_add_gtest(test_cigi_wire_conformance
      test/test_cigi_wire_conformance.cpp
    )
    target_include_directories(test_cigi_wire_conformance PRIVATE
      include
      ${CCL_INCLUDE_DIR}
    )
    target_link_libraries(test_cigi_wire_conformance ${CCL_LIBRARY})
  else()
    message(STATUS
      "CCL not found — test_cigi_wire_conformance skipped.\n"
      "  Install CCL (e.g. cmake --install /tmp/cigi-ccl/build --prefix /tmp/cigi-install)\n"
      "  to enable wire-format round-trip checks.")
  endif()
endif()

ament_package()
