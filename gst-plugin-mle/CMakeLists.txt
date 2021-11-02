cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_MLE
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
if (TFLITE_ENABLE)
  include_directories(${SYSROOT_INCDIR}/tflite)
endif()

link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
if (TFLITE_ENABLE)
  pkg_check_modules(TFLITE
    REQUIRED tensorflow-lite)
endif()

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)
add_definitions(-DANDROID)
if (DELEGATE_SUPPORT)
add_definitions(-DDELEGATE_SUPPORT)
endif()

# Common compiler flags.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-terminate -Wno-attributes -Wno-missing-field-initializers -Wno-unused-result")

add_subdirectory(mle_engine)
if (TFLITE_ENABLE)
add_subdirectory(mle_gst_tflite)
endif()
if (SNPE_ENABLE)
add_subdirectory(mle_gst_snpe)
endif()
