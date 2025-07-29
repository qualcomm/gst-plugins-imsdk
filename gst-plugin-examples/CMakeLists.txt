cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_EXAMPLES LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

# Check whether DRM PlayReady libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("media/drm/DrmAPI.h" HAVE_DRM_API_H)

# Check whether CDM libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("ce_cdm/cdm.h" HAVE_CDM_H)

# Check whether Auto-framing libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("iot-core-algs/auto-framing-alg.h" HAVE_AUTOFRAMING_ALGO_H)

# Check whether Camx libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("camx-api/camx/service/system/camera_metadata.h" HAVE_CAMX_API)

if (ENABLE_VIDEO_DECODE)
  add_subdirectory(gst-video-playback-example)
endif () # ENABLE_VIDEO_DECODE

if (ENABLE_VIDEO_DECODE AND HAVE_DRM_API_H)
  add_subdirectory(gst-drm-player-example)
endif () # ENABLE_VIDEO_DECODE AND HAVE_DRM_API_H

if (ENABLE_DISPLAY)
  add_subdirectory(gst-weston-composition)
endif () # ENABLE_DISPLAY

if (ENABLE_CAMERA)
  add_subdirectory(gst-appsink)
  add_subdirectory(gst-hotplug-pov-camera-app)
  add_subdirectory(gst-camera-metadata-example)
  add_subdirectory(gst-depth-assist-autofocus-example)
  add_subdirectory(gst-appsink-raw-plus-yuv)
  add_subdirectory(gst-camera-standby-example)
endif() # ENABLE_CAMERA

if (ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE)
  add_subdirectory(gst-concurrent-cameras-example)
  add_subdirectory(gst-single-4k-example)
  add_subdirectory(gst-two-1080p-example)
endif () # ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE

if (ENABLE_CAMERA AND ENABLE_DISPLAY)
  add_subdirectory(gst-camera-capture-example)
endif () # ENABLE_CAMERA AND ENABLE_DISPLAY

if (ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_DISPLAY)
  add_subdirectory(gst-timelapse-example)
  add_subdirectory(gst-buffering-encoding-mode-switch)
endif () # ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_DISPLAY

if (ENABLE_CAMERA AND (ENABLE_VIDEO_ENCODE OR ENABLE_DISPLAY))
  add_subdirectory(gst-add-remove-streams-runtime)
  add_subdirectory(gst-activate-deactivate-streams-runtime)
  add_subdirectory(gst-shdr-app)
  add_subdirectory(gst-camera-burst-intervalcapture-example)
  add_subdirectory(gst-camera-switch-example)
  add_subdirectory(gst-add-streams-as-bundle-example)
  add_subdirectory(gst-camera-switch-appsrc-example)
  add_subdirectory(gst-fastswitch-example)
endif () # ENABLE_CAMERA AND (ENABLE_VIDEO_ENCODE OR ENABLE_DISPLAY)

if (ENABLE_CAMERA AND ENABLE_ML AND HAVE_AUTOFRAMING_ALGO_H)
  add_subdirectory(gst-tracking-cam-app)
endif () # ENABLE_CAMERA AND ENABLE_ML AND HAVE_AUTOFRAMING_ALGO_H

if (ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_ML)
  add_subdirectory(gst-snpe-yolo-ssd-encode-example)
endif () # ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_ML

if (ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_DISPLAY)
  add_subdirectory(gst-three-1080p-example)
endif () # ENABLE_CAMERA AND ENABLE_VIDEO_ENCODE AND ENABLE_DISPLAY

if (ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_ML)
  add_subdirectory(gst-snpe-yolo-ssd-display-example)
  add_subdirectory(gst-tflite-posenet-display-example)
  add_subdirectory(gst-tflite-yolo-ssd-display-example)
endif () # ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_ML

if (ENABLE_WEBRTC)
  add_subdirectory(gst-webrtc-sendrecv-example)
endif () # ENABLE_WEBRTC
