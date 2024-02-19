cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGINS_QTI_OSS_CV_OPTCLFLOW
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
# Check whether CVP OpticalFlow header is present.
check_include_file_cxx("cvp/v2.0/cvpOpticalFlow.h" HAVE_CVP_OPTICALFLOW_H)
# Check whether EVA OpticalFlow header is present.
check_include_file_cxx("evaOpticalFlow.h" HAVE_EVA_OPTICALFLOW_H)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer CV OticalFlow plugin.
set(GST_CV_OPTCLFLOW gstqticvoptclflow)

add_library(${GST_CV_OPTCLFLOW} SHARED
  opticalflow.c
  $<$<BOOL:${HAVE_CVP_OPTICALFLOW_H}>:cvp-opticalflow-engine.cc>
  $<$<BOOL:${HAVE_EVA_OPTICALFLOW_H}>:eva-opticalflow-engine.cc>
)

target_include_directories(${GST_CV_OPTCLFLOW} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${GST_CV_OPTCLFLOW} PRIVATE
  $<$<BOOL:${HAVE_CVP_OPTICALFLOW_H}>:HAVE_CVP_OPTICALFLOW_H>
  $<$<BOOL:${HAVE_EVA_OPTICALFLOW_H}>:HAVE_EVA_OPTICALFLOW_H>
)

target_link_libraries(${GST_CV_OPTCLFLOW} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GBM_LIBRARIES}
  gstqtimemorybase
  gstqtiutilsbase
  $<$<BOOL:${HAVE_CVP_OPTICALFLOW_H}>:cvp2>
  $<$<BOOL:${HAVE_EVA_OPTICALFLOW_H}>:eva>
)

install(
  TARGETS ${GST_CV_OPTCLFLOW}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
