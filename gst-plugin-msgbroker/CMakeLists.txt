cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_QTI_OSS_MSGQUEUE
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# Create an interface library for qtimsgadaptor headers
add_library(qtimsgadaptor_interface INTERFACE)
target_include_directories(qtimsgadaptor_interface INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(msgpub)
add_subdirectory(msgsub)
add_subdirectory(msgadaptor)
add_subdirectory(protocol)
