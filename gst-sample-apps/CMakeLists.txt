cmake_minimum_required(VERSION 3.8.2)
project(GST_SAMPLE_APPS LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

if (ENABLE_AUDIO)
  add_subdirectory(gst-audio-decode-example)
  add_subdirectory(gst-audio-encode-example)
endif() # ENABLE_AUDIO

if (ENABLE_DISPLAY AND ENABLE_VIDEO_DECODE)
  add_subdirectory(gst-videocodec-concurrent-playback)
  add_subdirectory(gst-weston-composition-example)
endif () # ENABLE_DISPLAY AND ENABLE_VIDEO_DECODE

if (ENABLE_AUDIO AND ENABLE_DISPLAY AND ENABLE_VIDEO_DECODE)
  add_subdirectory(gst-audio-video-playback)
endif () # ENABLE_AUDIO AND ENABLE_DISPLAY AND ENABLE_VIDEO_DECODE

if (ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_VIDEO_ENCODE)
  add_subdirectory(gst-camera-single-stream-example)
endif () # ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_VIDEO_ENCODE

if (ENABLE_CAMERA)
  add_subdirectory(gst-appsink-example)
endif () # ENABLE_CAMERA

if (ENABLE_ML AND ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_VIDEO_ENCODE)
  add_subdirectory(gst-ai-classification)
  add_subdirectory(gst-ai-object-detection)
  add_subdirectory(gst-ai-pose-detection)
  add_subdirectory(gst-ai-segmentation)
  add_subdirectory(gst-ai-parallel-inference)
  add_subdirectory(gst-ai-multi-input-output-object-detection)
endif () # ENABLE_CAMERA AND ENABLE_DISPLAY AND ENABLE_VIDEO_ENCODE

if (ENABLE_VIDEO_DECODE)
  add_subdirectory(gst-video-playback-example)
endif () # ENABLE_VIDEO_DECODE
