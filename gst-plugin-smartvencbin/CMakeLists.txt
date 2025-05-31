cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGINS_QTI_OSS_SMART_ENCODE_BIN
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION} LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)

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
pkg_check_modules(GST_PBUTILS
  REQUIRED gstreamer-pbutils-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(QCOM_VIDEO_CTRL REQUIRED qcom-video-ctrl)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer Motion Estimator plugin.
set(GST_SMART_VENC_BIN gstqtismartvencbin)

add_library(${GST_SMART_VENC_BIN} SHARED
  vencbin.c
  smart-codec-engine.cc
)

target_include_directories(${GST_SMART_VENC_BIN} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_SMART_VENC_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_PBUTILS_LIBRARIES}
  gstqtivideobase
)

# Pass the VideoCtrl library version as a macro
target_compile_definitions(${GST_SMART_VENC_BIN} PRIVATE
  VIDEO_CTRL_VERSION="${QCOM_VIDEO_CTRL_VERSION_MAJOR}"
)

install(
  TARGETS ${GST_SMART_VENC_BIN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
