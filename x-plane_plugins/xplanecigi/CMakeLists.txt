cmake_minimum_required(VERSION 3.16)
project(xplanecigi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Locate X-Plane SDK ───────────────────────────────────────────────────────
# Download from https://developer.x-plane.com/sdk/plugin-sdk-downloads/
# and place at ~/x-plane_plugins/xplanecigi/SDK/
find_path(XPLANE_SDK_DIR
  NAMES CHeaders/XPLM/XPLMPlugin.h
  PATHS
    "${CMAKE_SOURCE_DIR}/SDK"
    "$ENV{HOME}/X-Plane-SDK"
    "$ENV{HOME}/x-plane_plugins/SDK"
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH   # SDK lives on the build machine, not in the mingw sysroot
)

if(NOT XPLANE_SDK_DIR)
  message(FATAL_ERROR
    "X-Plane SDK not found.\n"
    "Place SDK at ${CMAKE_SOURCE_DIR}/SDK/ or ~/X-Plane-SDK/ and re-run cmake.")
endif()

message(STATUS "X-Plane SDK: ${XPLANE_SDK_DIR}")

# ── Target ────────────────────────────────────────────────────────────────────
add_library(xplanecigi SHARED XPluginMain.cpp)

# X-Plane plugin defines (Windows 64-bit)
target_compile_definitions(xplanecigi PRIVATE
  XPLM200 XPLM210 XPLM300 XPLM301
  APL=0 IBM=1 LIN=0
)

target_include_directories(xplanecigi PRIVATE
  ${XPLANE_SDK_DIR}/CHeaders/XPLM
)

# ── XPLM library ─────────────────────────────────────────────────────────────
find_library(XPLM_LIB
  NAMES XPLM_64 XPLM
  PATHS "${XPLANE_SDK_DIR}/Libraries/Win"
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH   # SDK libs live on the build machine
)

target_link_libraries(xplanecigi PRIVATE
  ${XPLM_LIB}
  ws2_32
)

target_link_options(xplanecigi PRIVATE
  -shared
  -static-libgcc
  -static-libstdc++
  -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic
)

# ── Output as .xpl ───────────────────────────────────────────────────────────
set_target_properties(xplanecigi PROPERTIES
  PREFIX      ""
  SUFFIX      ".xpl"
  OUTPUT_NAME "xplanecigi"
)

install(TARGETS xplanecigi
  DESTINATION ${CMAKE_BINARY_DIR}/xplanecigi/win_x64)
