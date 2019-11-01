cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_QMMFSRC
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
pkg_check_modules(GST_AUDIO
  REQUIRED gstreamer-audio-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")

# GStreamer plugin.
set(GST_QMMFSRC_PLUGIN gstqmmfsrc)

add_library(${GST_QMMFSRC_PLUGIN} SHARED
  qmmf_source.c
  qmmf_source_utils.c
  qmmf_source_video_pad.c
  qmmf_source_audio_pad.c
  qmmf_source_image_pad.c
  qmmf_source_context.cc
)

target_include_directories(${GST_QMMFSRC_PLUGIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QMMFSRC_PLUGIN} PRIVATE
  qmmf_recorder_client
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_AUDIO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${GST_QMMFSRC_PLUGIN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)

