cmake_minimum_required(VERSION 3.8.2)
project(GST_PLUGIN_QTI_OSS_ML_TFLITE
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

include(CheckIncludeFile)
include(CheckIncludeFileCXX)

# Check for external delegate header
check_include_file("tensorflow/lite/delegates/external/external_delegate.h" HAVE_EXTERNAL_DELEGATE_H)

# Check whether the C API header for the TFLITE library is present.
check_include_file("tensorflow/lite/core/shims/c/c_api_experimental.h" HAVE_CORE_SHIMS_C_API_H)
check_include_file("tensorflow/lite/core/c/c_api_experimental.h" HAVE_CORE_C_API_H)
check_include_file("tensorflow/lite/c/c_api.h" HAVE_C_API_H)

# Version header not found only when building against TFLITE master branch headers
check_include_file("tensorflow/lite/version.h" HAVE_TFLITE_VERSION_H)

# Support for the External delegate appears after the tensorflow lite 2.10.0
# the reason is to determine if we are on a new or old version of tflite
# and on elder versions dynamic loading is not supported
# Either one of c_api header files is required for dynamic loading
if(HAVE_EXTERNAL_DELEGATE_H AND (HAVE_CORE_SHIMS_C_API_H OR HAVE_CORE_C_API_H OR HAVE_C_API_H))
  list(APPEND GST_QTI_ML_TFLITE_SRC
    ml-tflite-engine-c-api.cc
  )

  list(APPEND TARGET_COMPILE_DEFINITIONS
    $<$<BOOL:${HAVE_C_API_H}>:HAVE_C_API_H>
    $<$<BOOL:${HAVE_CORE_C_API_H}>:HAVE_CORE_C_API_H>
    $<$<BOOL:${HAVE_CORE_SHIMS_C_API_H}>:HAVE_CORE_SHIMS_C_API_H>
  )

  list(APPEND TARGET_INCS
    ${SYSROOT_INCDIR}/tensorflow
  )

  list(APPEND TARGET_LIBRARIES
    ${CMAKE_DL_LIBS}
  )
else()
  list(APPEND GST_QTI_ML_TFLITE_SRC
    ml-tflite-engine.cc
  )

  # Prefer the prebuilt TFLite package.
  pkg_check_modules(TFLITE tensorflow-lite-prebuilt)
  if(NOT TFLITE_FOUND)
    pkg_check_modules(TFLITE REQUIRED tensorflow-lite)
  endif()

  include(CheckIncludeFile)

  # Check for hexagon delegate header
  if(${TFLITE_VERSION} VERSION_GREATER 2.2.0)
    check_include_file("tensorflow/lite/delegates/hexagon/hexagon_delegate.h" HAVE_HEXAGON_DELEGATE_H)
  else()
    check_include_file("tensorflow/lite/experimental/delegates/hexagon/hexagon_delegate.h" HAVE_HEXAGON_DELEGATE_H)
  endif()

  list(APPEND TARGET_INCS
    $<$<VERSION_LESS_EQUAL:${TFLITE_VERSION},2.2.0>:${SYSROOT_INCDIR}/tflite>
    $<$<VERSION_GREATER:${TFLITE_VERSION},2.2.0>:${SYSROOT_INCDIR}/tensorflow>
  )

  list(APPEND TARGET_LIBRARIES
    ${TFLITE_LIBRARIES}
    $<$<VERSION_LESS_EQUAL:${TFLITE_VERSION},2.2.0>:gpu_delegate>
    $<$<VERSION_LESS_EQUAL:${TFLITE_VERSION},2.2.0>:xnnpack_delegate>
  )
endif()

# Generate configuration header file.
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

# GStreamer ML TFLite plugin.
set(GST_QTI_ML_TFLITE gstqtimltflite)

add_library(${GST_QTI_ML_TFLITE} SHARED
  mltflite.c
  ${GST_QTI_ML_TFLITE_SRC}
)

target_compile_definitions(${GST_QTI_ML_TFLITE} PRIVATE
  $<$<BOOL:${HAVE_HEXAGON_DELEGATE_H}>:HAVE_HEXAGON_DELEGATE_H>
  $<$<BOOL:${HAVE_EXTERNAL_DELEGATE_H}>:HAVE_EXTERNAL_DELEGATE_H>
  ${TARGET_COMPILE_DEFINITIONS}
)

target_include_directories(${GST_QTI_ML_TFLITE} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${TARGET_INCS}
)

target_link_libraries(${GST_QTI_ML_TFLITE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${TARGET_LIBRARIES}
  gstqtimlbase
  gstqtiutilsbase
)

install(
  TARGETS ${GST_QTI_ML_TFLITE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
