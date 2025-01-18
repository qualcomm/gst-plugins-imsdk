cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGINS_QTI_OSS_BASE LANGUAGES C CXX)

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

include(CheckIncludeFile)
include(CheckIncludeFileCXX)

# Check whether MMM color format header is present.
check_include_file("display/media/mmm_color_fmt.h" HAVE_MMM_COLOR_FMT_H)

# Check whether MSM media info header is present.
check_include_file("media/msm_media_info.h" HAVE_MSM_MEDIA_INFO_H)

# Check whether DMA HEAP header is present.
check_include_file("linux/dma-heap.h" HAVE_LINUX_DMA_HEAP_H)

# Check whether Adreno C2D header is present.
check_include_file("adreno/c2d2.h" HAVE_ADRENO_C2D2_H)

# Check whether IOT CORE IB2C header is present.
check_include_file_cxx("iot-core-algs/ib2c.h" HAVE_IOT_CORE_IB2C_H)

# Check whether FastCV header is present.
check_include_file("fastcv/fastcv.h" HAVE_FASTCV_H)

# Check whether GBM header is present.
check_include_file("gbm_priv.h" HAVE_GBM_PRIV_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# Common CXX compiler flags.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

include_directories(
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

add_subdirectory(gst/utils)
add_subdirectory(gst/video)
add_subdirectory(gst/memory)
add_subdirectory(gst/ml)
add_subdirectory(gst/cv)
