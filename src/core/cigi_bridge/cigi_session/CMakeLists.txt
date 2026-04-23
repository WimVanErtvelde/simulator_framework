# cigi_session — typed CCL wrapper used by both host node and plugin.
# Two build modes:
#   (a) As a subdirectory of the cigi_bridge ROS2 package (Linux native).
#   (b) As a subdirectory of the xplanecigi plugin (mingw64 cross).
# CMAKE_CURRENT_SOURCE_DIR is the same in both; CCL must already be
# found by the parent CMakeLists.

if(NOT DEFINED CCL_INCLUDE_DIR OR NOT DEFINED CCL_LIBRARY)
  message(FATAL_ERROR "cigi_session requires CCL_INCLUDE_DIR and CCL_LIBRARY from parent CMakeLists")
endif()

add_library(cigi_session STATIC
  src/HostSession.cpp
  src/IgSession.cpp
  src/emitters.cpp
)

target_include_directories(cigi_session PUBLIC
  include
  ${CCL_INCLUDE_DIR}
)

target_link_libraries(cigi_session PUBLIC ${CCL_LIBRARY})

target_compile_features(cigi_session PUBLIC cxx_std_17)

if(BUILD_TESTING AND CMAKE_PROJECT_NAME STREQUAL "sim_cigi_bridge")
  # gtest only available on the ROS2 side. Plugin build skips this.
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_cigi_session test/test_cigi_session.cpp)
  target_link_libraries(test_cigi_session cigi_session)
endif()
