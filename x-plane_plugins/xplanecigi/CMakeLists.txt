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

# ── cigi_session library (cross-compiled for mingw64 alongside the plugin)
# The library itself lives under src/core/cigi_bridge/cigi_session/ and is
# shared with the ROS2 host node. Here we compile its sources into the plugin
# build using the mingw64 toolchain. CCL is linked as a static archive
# produced by the matching cross-build at references/CIGI/cigi3.3/ccl/install-mingw.
set(CCL_ROOT_MINGW "${CMAKE_SOURCE_DIR}/../../references/CIGI/cigi3.3/ccl/install-mingw")
set(CCL_INCLUDE_DIR "${CCL_ROOT_MINGW}/include/cigicl")
find_library(CCL_LIBRARY
  NAMES cigicl_static cigicl
  PATHS "${CCL_ROOT_MINGW}/lib"
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH
)
if(NOT CCL_LIBRARY)
  message(FATAL_ERROR
    "CCL cross-build not found at ${CCL_ROOT_MINGW}/lib.\n"
    "Run Task 0.1: cmake -B build-mingw -DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/toolchain-mingw64.cmake \\\n"
    "              && cmake --build build-mingw -j4 \\\n"
    "              && cmake --install build-mingw --prefix \"\$(pwd)/install-mingw\"\n"
    "in references/CIGI/cigi3.3/ccl/")
endif()
add_subdirectory(
  "${CMAKE_SOURCE_DIR}/../../src/core/cigi_bridge/cigi_session"
  "${CMAKE_BINARY_DIR}/cigi_session")

# ── Target ────────────────────────────────────────────────────────────────────
add_library(xplanecigi SHARED XPluginMain.cpp)

target_link_libraries(xplanecigi PRIVATE cigi_session)

# X-Plane plugin defines (Windows 64-bit).
# XPLM400: XPLMWeatherInfo_t / Winds / Clouds structs (Slice 3b regional weather)
# XPLM420: XPLMBeginWeatherUpdate / XPLMEndWeatherUpdate /
#          XPLMSetWeatherAtLocation / XPLMEraseWeatherAtLocation,
#          XPLMWeatherInfo_t::radius_nm / max_altitude_msl_ft,
#          XPLM_DEFAULT_WXR_* constants.
# No linker changes — same XPLM library, just header gates.
target_compile_definitions(xplanecigi PRIVATE
  XPLM200 XPLM210 XPLM300 XPLM301 XPLM400 XPLM420
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
