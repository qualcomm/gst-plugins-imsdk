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

# Check whether MMM color format header is present.
include(CheckIncludeFile)
check_include_file("display/media/mmm_color_fmt.h" HAVE_MMM_COLOR_FMT_H)

# Check whether DMA HEAP header is present.
include(CheckIncludeFile)
check_include_file("linux/dma-heap.h" HAVE_LINUX_DMA_HEAP_H)

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

add_subdirectory(gst/video)
add_subdirectory(gst/memory)
add_subdirectory(gst/ml)
add_subdirectory(gst/cv)
