cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_CAMERA_REPROCESS
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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# Gstreamer plugin
set(CAMERA_REPROCESS gstqticamreproc)

add_library(${CAMERA_REPROCESS} SHARED
  camera-reprocess-context.cc
  camera-reprocess.c
)

target_include_directories(${CAMERA_REPROCESS} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${CAMERA_REPROCESS} PRIVATE
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.0>:CAMERA_METADATA_1_0>
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.1>:CAMERA_METADATA_1_1>
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.0ns>:CAMERA_METADATA_1_0_NS>
)

target_link_libraries(${CAMERA_REPROCESS} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  qmmf_recorder_client
  qmmf_camera_metadata
  gstqtiutilsbase
  gstqtivideobase
)

install(
  TARGETS ${CAMERA_REPROCESS}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
