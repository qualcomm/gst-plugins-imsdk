cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGINS_QTI_OSS_DFS
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer DFS plugin.
set(GST_DFS gstqtidfs)

add_library(${GST_DFS} SHARED
  dfs.c
  dfs-engine.cc
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_DFS} PRIVATE
  $<$<STREQUAL:${TARGET_BOARD_PLATFORM},qrb5165>:TARGET_BOARD_QRB5165>
)

target_include_directories(${GST_DFS} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${GST_DFS} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtivideobase
  rv
  fastcvopt
  $<$<STREQUAL:${TARGET_BOARD_PLATFORM},qrb5165>:mv1>
)

install(
  TARGETS ${GST_DFS}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
