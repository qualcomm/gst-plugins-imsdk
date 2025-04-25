cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_FDTRANSFER
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C
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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# GStreamer source plugin.
set(GST_QTI_SOCKETSRC qtisocketsrc)

add_library(${GST_QTI_SOCKETSRC} SHARED
  qtisocketsrc.c
  qtifdsocket.c
)

target_include_directories(${GST_QTI_SOCKETSRC} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_SOCKETSRC} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_SOCKETSRC} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtimlbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_SOCKETSRC}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

# GStreamer sink plugin.
set(GST_QTI_SOCKETSINK qtisocketsink)

add_library(${GST_QTI_SOCKETSINK} SHARED
  qtisocketsink.c
  qtifdsocket.c
)

target_include_directories(${GST_QTI_SOCKETSINK} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_SOCKETSINK} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_SOCKETSINK} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtimlbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_SOCKETSINK}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
