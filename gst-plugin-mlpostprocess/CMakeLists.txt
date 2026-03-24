cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_QTI_OSS_ML_POST_PROCESS
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
set(CMAKE_PREFIX_PATH "${PKG_CONFIG_LOCAL_PATH}")

pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(CAIRO REQUIRED cairo)
pkg_check_modules(GLIB_JSON REQUIRED json-glib-1.0)
pkg_check_modules(GST_QCOM_UTILS REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_VIDEO REQUIRED gstreamer-qcom-oss-video-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_ALLOC REQUIRED gstreamer-qcom-oss-allocators-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_ML REQUIRED gstreamer-qcom-oss-ml-1.0>=1.0.0)

# Generate configuration header file with plugin describing definitions
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Common CXX compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# GStreamer ML post process plugin.
set(GST_QTI_ML_POST_PROCESS gstqtimlpostprocess)

add_library(${GST_QTI_ML_POST_PROCESS} SHARED
  mlpostprocess.c
  ml-post-process-utils.c
  ml-post-process-engine.cc
)

target_compile_definitions(${GST_QTI_ML_POST_PROCESS} PRIVATE
  GST_ML_MODULES_DIR="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/imsdk/qtimlpostprocess/modules"
)

target_include_directories(${GST_QTI_ML_POST_PROCESS} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${GLIB_JSON_INCLUDE_DIRS}
  ${GST_QCOM_UTILS_INCLUDE_DIRS}
  ${GST_QCOM_ALLOC_INCLUDE_DIRS}
  ${GST_QCOM_VIDEO_INCLUDE_DIRS}
  ${GST_QCOM_ML_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_POST_PROCESS} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${GLIB_JSON_LIBRARIES}
  ${GST_QCOM_UTILS_LIBRARIES}
  ${GST_QCOM_ALLOC_LIBRARIES}
  ${GST_QCOM_VIDEO_LIBRARIES}
  ${GST_QCOM_ML_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_ML_POST_PROCESS}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

add_subdirectory(modules)
