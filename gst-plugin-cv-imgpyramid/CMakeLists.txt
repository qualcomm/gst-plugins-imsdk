cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_CV_IMGPYRAMID
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
pkg_check_modules(GBM REQUIRED gbm)

include(CheckIncludeFileCXX)
# Check whether CVP pyramid header is present.
check_include_file_cxx("cvp/v2.0/cvpPyramid.h" HAVE_CVP_IMGPYRAMID_H)
# Check whether EVA pyramid header is present.
check_include_file_cxx("evaPyramid.h" HAVE_EVA_IMGPYRAMID_H)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Common compiler flags.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer plugin.
set(GST_QTI_CV_IMGPYRAMID gstqticvimgpyramid)

add_library(${GST_QTI_CV_IMGPYRAMID} SHARED
  imagepyramid.c
  imagepyramidpads.c
  $<$<BOOL:${HAVE_CVP_IMGPYRAMID_H}>:cvp-imgpyramid-engine.cc>
  $<$<BOOL:${HAVE_EVA_IMGPYRAMID_H}>:eva-imgpyramid-engine.cc>
)

target_compile_definitions(${GST_QTI_CV_IMGPYRAMID} PRIVATE
  HAVE_CONFIG_H
  $<$<BOOL:${HAVE_CVP_IMGPYRAMID_H}>:HAVE_CVP_IMGPYRAMID_H>
  $<$<BOOL:${HAVE_EVA_IMGPYRAMID_H}>:HAVE_EVA_IMGPYRAMID_H>
)

target_include_directories(${GST_QTI_CV_IMGPYRAMID} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_CV_IMGPYRAMID} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GBM_LIBRARIES}
  gstqtimemorybase
  $<$<BOOL:${HAVE_CVP_IMGPYRAMID_H}>:cvp2>
  $<$<BOOL:${HAVE_EVA_IMGPYRAMID_H}>:eva>
)

install(
  TARGETS ${GST_QTI_CV_IMGPYRAMID}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
