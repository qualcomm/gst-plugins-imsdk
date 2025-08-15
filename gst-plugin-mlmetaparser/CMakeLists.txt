cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_META_PARSE
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C
)

include_directories(
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

# GStreamer Meta Parse plugin.
set(GST_QTI_ML_META_PARSER gstqtimlmetaparser)

add_library(${GST_QTI_ML_META_PARSER} SHARED
  mlmetaparser.c
  parsermodule.c
)

target_include_directories(${GST_QTI_ML_META_PARSER} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${GST_QTI_ML_META_PARSER} PRIVATE
  GST_PARSER_MODULES_DIR="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/mlmetaparser/modules"
)

target_link_libraries(${GST_QTI_ML_META_PARSER} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_ML_META_PARSER}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

add_subdirectory(modules)
