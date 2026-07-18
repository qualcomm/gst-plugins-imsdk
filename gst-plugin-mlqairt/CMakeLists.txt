cmake_minimum_required(VERSION 3.16)
project(GST_PLUGIN_QTI_OSS_ML_QAIRT
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
pkg_check_modules(GST_QCOM_UTILS REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_ML REQUIRED gstreamer-qcom-oss-ml-1.0>=1.0.0)

# Generate configuration header file with plugin describing definitions
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
# The QAIRT C++ SDK headers (e.g. QairtGraph.hpp) declare stub methods with a
# 'const' qualifier on a by-value return type, which triggers -Wignored-qualifiers.
# Keep -Werror for our own code but do not let this vendor-header warning be fatal.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=ignored-qualifiers")

# GStreamer ML QAIRT plugin.
set(GST_QTI_ML_QAIRT gstqtimlqairt)

add_library(${GST_QTI_ML_QAIRT} SHARED
  mlqairt.c
  ml-qairt-engine.cc
)

# The QAIRT C++ API is header-only. The system and backend libraries are loaded
# at runtime via dlopen, so only the include directories and libdl are needed at
# build time. Both the C API root and the C++ header root must be on the include
# path (the C++ headers include the C headers by their QAIRT-relative path).
target_include_directories(${GST_QTI_ML_QAIRT} PUBLIC
  ${CMAKE_SYSROOT}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/QAIRT
  ${CMAKE_SYSROOT}/${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/QAIRT/QairtCpp
  ${GST_INCLUDE_DIRS}
  ${GST_QCOM_UTILS_INCLUDE_DIRS}
  ${GST_QCOM_ML_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_QAIRT} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_QCOM_ML_LIBRARIES}
  ${GST_QCOM_UTILS_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_ML_QAIRT}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
