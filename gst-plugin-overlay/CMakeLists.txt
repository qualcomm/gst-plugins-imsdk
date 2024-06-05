cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_OVERLAY
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

# Check whether C2D header is present.
include(CheckIncludeFile)
check_include_file("adreno/c2d2.h" HAVE_C2D_H)

# Check whether IB2C header is present.
include(CheckIncludeFileCXX)
check_include_file_cxx("iot-core-algs/ib2c.h" HAVE_GLES_H)

# Check whether MMM color format header is present.
include(CheckIncludeFile)
check_include_file("display/media/mmm_color_fmt.h" HAVE_MMM_COLOR_FMT_H)

# Check whether DMA HEAP header is present.
include(CheckIncludeFileCXX)
check_include_file_cxx("linux/dma-heap.h" HAVE_LINUX_DMA_HEAP_H)

# Check whether DMA header is present.
include(CheckIncludeFile)
check_include_file("linux/dma-buf.h" HAVE_LINUX_DMA_BUF_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# GStreamer plugin.
set(GST_QTI_OVERLAY qtioverlay)

add_library(${GST_QTI_OVERLAY} SHARED
  gstoverlay.cc
)

target_include_directories(${GST_QTI_OVERLAY} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${KERNEL_BUILDDIR}/usr/include
)

target_compile_definitions(${GST_QTI_OVERLAY} PRIVATE
  $<$<BOOL:${HAVE_C2D_H}>:ENABLE_C2D>
  $<$<BOOL:${HAVE_GLES_H}>:ENABLE_GLES>
)

target_link_libraries(${GST_QTI_OVERLAY} PRIVATE
  overlay_lib
  qtimlmeta
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqticvbase
)

install(
  TARGETS ${GST_QTI_OVERLAY}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

add_subdirectory(overlay-lib)
