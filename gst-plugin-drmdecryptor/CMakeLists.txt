cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_DECRYPTOR
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

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Check whether CDM libs are present.
include(CheckIncludeFileCXX)
check_include_file_cxx("ce_cdm/cdm.h" HAVE_CDM_H)

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)
if (HAVE_CDM_H)
  add_definitions(-DENABLE_WIDEVINE)
endif()

# Common compiler flags.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer plugin.
set(GST_QTI_DRM_DECRYPTOR gstqtidrmdecryptor)

add_library(${GST_QTI_DRM_DECRYPTOR} SHARED
  drmdecryptor.cc
  drmdecryptor-engine.cc
)

target_include_directories(${GST_QTI_DRM_DECRYPTOR} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_DRM_DECRYPTOR} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  cutils
  gstqtimemorybase
  $<$<BOOL:${HAVE_CDM_H}>:widevine_ce_cdm_shared>
  $<$<BOOL:${HAVE_CDM_H}>:oemcrypto>
)

install(
  TARGETS ${GST_QTI_DRM_DECRYPTOR}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
