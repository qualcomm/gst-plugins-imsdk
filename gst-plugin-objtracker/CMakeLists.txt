cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_OBJTRACKER
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
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer Object Tracker plugin.
set(GST_QTI_OBJTRACKER gstqtiobjtracker)

add_library(${GST_QTI_OBJTRACKER} SHARED
  objtracker.c
  objtracker-algo.cc
)

set(GST_QTI_OBJTRACKER_ALGO gstqitobjtrackeralgo)
add_library(${GST_QTI_OBJTRACKER_ALGO} INTERFACE)
target_include_directories(${GST_QTI_OBJTRACKER_ALGO} INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(${GST_QTI_OBJTRACKER} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${GST_QTI_OBJTRACKER} PRIVATE
  GST_QTI_OBJTRACKER_ALGORITHM="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/algorithm"
)

target_link_libraries(${GST_QTI_OBJTRACKER} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_OBJTRACKER}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

add_subdirectory(algorithm/bytetrack)
