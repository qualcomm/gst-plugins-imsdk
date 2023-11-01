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

# Check whether DRM PlayReady libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("media/drm/DrmAPI.h" HAVE_DRM_API_H)

if (ENABLE_TRACKING_CAM)
  add_subdirectory(gst-tracking-cam-app)
endif()
add_subdirectory(gst-weston-composition)
add_subdirectory(gst-appsink)
add_subdirectory(gst-add-remove-streams-runtime)
add_subdirectory(gst-activate-deactivate-streams-runtime)
add_subdirectory(gst-hotplug-pov-camera-app)
add_subdirectory(gst-shdr-app)
add_subdirectory(gst-camera-capture-example)
add_subdirectory(gst-camera-metadata-example)
add_subdirectory(gst-depth-assist-autofocus-example)
add_subdirectory(gst-appsink-raw-plus-yuv)
add_subdirectory(gst-camera-switch-example)
add_subdirectory(gst-add-streams-as-bundle-example)
add_subdirectory(gst-concurrent-cameras-example)
add_subdirectory(gst-single-4k-example)
add_subdirectory(gst-two-1080p-example)
add_subdirectory(gst-three-1080p-example)
add_subdirectory(gst-snpe-yolo-ssd-encode-example)
add_subdirectory(gst-snpe-yolo-ssd-display-example)
add_subdirectory(gst-camera-switch-appsrc-example)
if (HAVE_DRM_API_H)
  add_subdirectory(gst-drm-player-example)
endif()
add_subdirectory(gst-tflite-posenet-display-example)
add_subdirectory(gst-tflite-yolo-ssd-display-example)
add_subdirectory(gst-camera-standby-example)
add_subdirectory(gst-fastswitch-example)