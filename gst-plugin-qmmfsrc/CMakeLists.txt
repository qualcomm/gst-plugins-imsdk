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
set(GST_QMMFSRC_PLUGIN gstqtiqmmfsrc)

add_library(${GST_QMMFSRC_PLUGIN} SHARED
  qmmf_source.c
  qmmf_source_video_pad.c
  qmmf_source_image_pad.c
  qmmf_source_utils.c
  qmmf_source_context.cc
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_QMMFSRC_PLUGIN} PRIVATE
  GST_IMAGE_MAX_WIDTH="${GST_IMAGE_MAX_WIDTH}"
  GST_IMAGE_MAX_HEIGHT="${GST_IMAGE_MAX_HEIGHT}"
  GST_VIDEO_MAX_WIDTH="${GST_VIDEO_MAX_WIDTH}"
  GST_VIDEO_MAX_HEIGHT="${GST_VIDEO_MAX_HEIGHT}"
  GST_VIDEO_MAX_FPS="${GST_VIDEO_MAX_FPS}"
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.0>:CAMERA_METADATA_1_0>
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.1>:CAMERA_METADATA_1_1>
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.0ns>:CAMERA_METADATA_1_0_NS>
  $<$<BOOL:${GST_VIDEO_YUY2_FORMAT_ENABLE}>:GST_VIDEO_YUY2_FORMAT_ENABLE>
  $<$<BOOL:${GST_VIDEO_UYVY_FORMAT_ENABLE}>:GST_VIDEO_UYVY_FORMAT_ENABLE>
  $<$<BOOL:${GST_VIDEO_P010_10LE_FORMAT_ENABLE}>:GST_VIDEO_P010_10LE_FORMAT_ENABLE>
  $<$<BOOL:${GST_VIDEO_NV12_10LE32_FORMAT_ENABLE}>:GST_VIDEO_NV12_10LE32_FORMAT_ENABLE>
  $<$<BOOL:${GST_IMAGE_NV12_FORMAT_ENABLE}>:GST_IMAGE_NV12_FORMAT_ENABLE>
  $<$<BOOL:${GST_VIDEO_TYPE_SUPPORT}>:GST_VIDEO_TYPE_SUPPORT>
  $<$<BOOL:${MULTI_CAMERA_ENABLE}>:MULTI_CAMERA_ENABLE>
)

target_include_directories(${GST_QMMFSRC_PLUGIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  $<$<STREQUAL:${CAMERA_SERVICE},LECAM>:${SYSROOT_INCDIR}/camx-api/camx/service>
)

target_link_libraries(${GST_QMMFSRC_PLUGIN} PRIVATE
  qmmf_recorder_client
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${GST_QMMFSRC_PLUGIN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

