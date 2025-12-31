cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_QTI_OSS_ML_ONNX
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST REQUIRED
  gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE REQUIRED
  gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC REQUIRED
  gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_QCOM_ML
  REQUIRED gstreamer-qcom-oss-ml-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_UTILS
  REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)

# Find ONNX Runtime using pkg-config
pkg_check_modules(ONNXRUNTIME REQUIRED libonnxruntime)

include(CheckIncludeFile)
include(CheckIncludeFileCXX)

# Check for ONNX Runtime headers
check_include_file_cxx("onnxruntime/onnxruntime_c_api.h" HAVE_ONNXRUNTIME_H)
if(NOT HAVE_ONNXRUNTIME_H)
  message(FATAL_ERROR "ONNX Runtime headers not found.")
endif()

# Check for ONNX Proto headers
check_include_file_cxx("onnx/onnx-ml.pb.h" HAVE_ONNXPROTO_H)
if(NOT HAVE_ONNXPROTO_H)
  message(FATAL_ERROR "ONNX Proto headers not found.")
endif()

# Find qnn execution provider library
find_library(QNNPROVIDERS_LIB onnxruntime_providers_qnn
  PATHS ${SYSROOT_LIBDIR}
  NO_DEFAULT_PATH
)
if(NOT QNNPROVIDERS_LIB)
  message(FATAL_ERROR "QNN Providers library not found.")
endif()

# Find ONNX proto library
find_library(ONNX_PROTO_LIB onnx_proto
  PATHS ${SYSROOT_LIBDIR}
  NO_DEFAULT_PATH
)
if(NOT ONNX_PROTO_LIB)
  message(FATAL_ERROR "ONNX proto library not found.")
endif()

list(APPEND GST_QTI_ML_ONNX_SRC
  mlonnx.c
  ml-onnx-engine.cc
)

list(APPEND TARGET_LIBRARIES
  ${ONNXRUNTIME_LIBRARIES}
  ${QNNPROVIDERS_LIB}
)

# Add ONNX proto library if found
if(ONNX_PROTO_LIB)
  list(APPEND TARGET_LIBRARIES ${ONNX_PROTO_LIB})
endif()

# Generate configuration header file with plugin describing definitions.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

# GStreamer ML ONNX plugin.
set(GST_QTI_ML_ONNX gstqtimlonnx)

add_library(${GST_QTI_ML_ONNX} SHARED
  ${GST_QTI_ML_ONNX_SRC}
)

target_compile_definitions(${GST_QTI_ML_ONNX} PRIVATE
  ${TARGET_COMPILE_DEFINITIONS}
)

target_include_directories(${GST_QTI_ML_ONNX} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${TARGET_INCS}
  ${ONNXRUNTIME_INCLUDE_DIRS}
  ${GST_QCOM_UTILS_INCLUDE_DIRS}
  ${GST_QCOM_ML_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_ONNX} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${TARGET_LIBRARIES}
  ${GST_QCOM_ML_LIBRARIES}
  ${GST_QCOM_UTILS_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_ML_ONNX}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
