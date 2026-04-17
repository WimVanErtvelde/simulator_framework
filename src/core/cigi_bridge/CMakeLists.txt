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
    ${CMAKE_CURRENT_SOURCE_DIR}/cigi_ig_interface
  NO_DEFAULT_PATH
)

if(CCL_INCLUDE_DIR)
  message(STATUS "CCL found at ${CCL_INCLUDE_DIR} — building vendored CIGI_IG_Interface")

  find_library(CCL_LIBRARY
    NAMES cigicl CigiCCL
    PATHS /usr/lib /usr/local/lib
  )

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
else()
  message(STATUS
    "CCL (CigiIGSession.h) not found — vendored CIGI_IG_Interface skipped.\n"
    "  Install CCL headers to /usr/local/include and re-run cmake to enable.\n"
    "  The cigi_bridge_node still builds and sends raw CIGI 3.3 packets without CCL.")
endif()

ament_package()
