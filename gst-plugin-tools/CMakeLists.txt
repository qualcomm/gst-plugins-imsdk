cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_TOOLS LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
set(CMAKE_PREFIX_PATH "${PKG_CONFIG_LOCAL_PATH}")

pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_RTSP REQUIRED gstreamer-rtsp-server-1.0>=${GST_VERSION_REQUIRED})

add_subdirectory(gst-pipeline-app)
add_subdirectory(gst-rtsp-server)
add_subdirectory(gst-warmboot-app)
