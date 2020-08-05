cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_EXAMPLES LANGUAGES C)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

add_subdirectory(gst-tracking-cam-app)
add_subdirectory(gst-weston-composition)
add_subdirectory(gst-appsink)
