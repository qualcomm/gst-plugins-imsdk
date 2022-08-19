cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_EXAMPLES LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

if (ENABLE_TRACKING_CAM)
  add_subdirectory(gst-tracking-cam-app)
endif()
add_subdirectory(gst-weston-composition)
add_subdirectory(gst-appsink)
add_subdirectory(gst-add-remove-streams-runtime)
add_subdirectory(gst-activate-deactivate-streams-runtime)
add_subdirectory(gst-hotplug-pov-camera-app)
add_subdirectory(gst-shdr-app)
add_subdirectory(gst-camera-metadata-example)
add_subdirectory(gst-appsink-raw-plus-yuv)
