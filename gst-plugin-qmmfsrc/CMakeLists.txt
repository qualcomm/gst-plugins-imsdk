cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_QTI_OSS_QMMFSRC
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR} ${SYSROOT_LIBDIR}/${TARGET_BOARD_PLATFORM})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_QCOM_UTILS
  REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_VIDEO
  REQUIRED gstreamer-qcom-oss-video-1.0>=1.0.0)

# Generate configuration header file with plugin describing definitions
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable")

# Check whether ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION is supported
include(CheckCXXSourceCompiles)

set(SRC
"
#include \"system/camera_metadata.h\"
int main()
{
    if (0 == ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION)
        return -1;
    return 0;
}")

check_cxx_source_compiles("${SRC}"
  HAS_ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION)

# Check whether ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR is supported
set(SRC_ULTRA_HIGH_RES
"
#include \"system/camera_metadata.h\"
int main()
{
    if (0 == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR)
        return -1;
    return 0;
}")

check_cxx_source_compiles("${SRC_ULTRA_HIGH_RES}"
  HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR)

# Check whether ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION is supported
set(SRC_MAX_RES_CONFIG
"
#include \"system/camera_metadata.h\"
int main()
{
    if (0 == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)
        return -1;
    return 0;
}")

check_cxx_source_compiles("${SRC_MAX_RES_CONFIG}"
  HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)

# GStreamer plugin.
set(GST_QMMFSRC_PLUGIN gstqtiqmmfsrc)

add_library(${GST_QMMFSRC_PLUGIN} SHARED
  qmmf_source.c
  qmmf_source_video_pad.c
  qmmf_source_image_pad.c
  qmmf_source_utils.cc
  qmmf_source_context.cc
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_QMMFSRC_PLUGIN} PRIVATE
  $<$<BOOL:${VHDR_MODES_ENABLE}>:VHDR_MODES_ENABLE>
  $<$<BOOL:${FEATURE_LOGICAL_CAMERA_SENSOR_SWITCH}>:FEATURE_LOGICAL_CAMERA_SENSOR_SWITCH>
  $<$<BOOL:${HAS_ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION}>:HAS_ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION>
  $<$<BOOL:${HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR}>:HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR>
  $<$<BOOL:${HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION}>:HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION>
)

target_include_directories(${GST_QMMFSRC_PLUGIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}/camx-api/camx/service
  ${GST_QCOM_UTILS_INCLUDE_DIRS}
  ${GST_QCOM_VIDEO_INCLUDE_DIRS}
)

target_link_libraries(${GST_QMMFSRC_PLUGIN} PRIVATE
  qmmf_recorder_client
  qmmf_camera_metadata
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_QCOM_UTILS_LIBRARIES}
  ${GST_QCOM_VIDEO_LIBRARIES}
  gstqticamerabase
)

install(
  TARGETS ${GST_QMMFSRC_PLUGIN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
