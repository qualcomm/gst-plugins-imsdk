cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_ML_VIDEO_DETECTION
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${SYSROOT_LIBDIR}/pkgconfig")

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(CAIRO REQUIRED cairo)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Check whether DMA header is present.
include(CheckIncludeFile)
check_include_file("linux/dma-buf.h" HAVE_LINUX_DMA_BUF_H)

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer ML video categorization plugin.
set(GST_QTI_ML_VIDEO_DETECTION gstqtimlvdetection)

add_library(${GST_QTI_ML_VIDEO_DETECTION} SHARED
  mlvdetection.c
)

set_target_properties(${GST_QTI_ML_VIDEO_DETECTION} PROPERTIES PUBLIC_HEADER
  "modules/ml-video-detection-module.h"
)

target_compile_definitions(${GST_QTI_ML_VIDEO_DETECTION} PRIVATE
  $<$<BOOL:${HAVE_LINUX_DMA_BUF_H}>:HAVE_LINUX_DMA_BUF_H>
)

target_include_directories(${GST_QTI_ML_VIDEO_DETECTION} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_ML_VIDEO_DETECTION} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${CAIRO_LIBRARIES}
  gstqtimlbase
  gstqtivideobase
  gstqtimemorybase
)

install(
  TARGETS ${GST_QTI_ML_VIDEO_DETECTION}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/ml/
)

add_subdirectory(modules)
