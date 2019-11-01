cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_TOOLS LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

add_subdirectory(gst-pipeline-app)
