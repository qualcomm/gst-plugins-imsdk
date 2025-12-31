cmake_minimum_required(VERSION 3.16)
project(GST_PLUGINS_QTI_OSS_DFS
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
pkg_check_modules(GST_QCOM_UTILS
  REQUIRED gstreamer-qcom-oss-utils-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_VIDEO
  REQUIRED gstreamer-qcom-oss-video-1.0>=1.0.0)
pkg_check_modules(GST_QCOM_ALLOC
  REQUIRED gstreamer-qcom-oss-allocators-1.0>=1.0.0)

# Generate configuration header file.
configure_file(config.h.in config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Precompiler definitions.
add_definitions(-DHAVE_CONFIG_H)

# Common compiler flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined")

# GStreamer DFS plugin.
set(GST_DFS gstqtidfs)

include(CheckIncludeFileCXX)
check_include_file_cxx("dfs_factory.h" HAVE_DFS_FACTORY_H)

if(HAVE_DFS_FACTORY_H)

  add_library(${GST_DFS} SHARED
    dfs.c
    dfs-engine.cc
  )

  # Add target specific precompiler defintions.
  set(RVSDK_API_VERSION "0x202404" CACHE STRING "RVSDK_API_VERSION")
  target_compile_definitions(${GST_DFS} PRIVATE
    RVSDK_API_VERSION=${RVSDK_API_VERSION})

  target_include_directories(${GST_DFS} PRIVATE
    ${GST_INCLUDE_DIRS}
  )

  target_link_libraries(${GST_DFS} PRIVATE
    ${GST_LIBRARIES}
    ${GST_BASE_LIBRARIES}
    ${GST_ALLOC_LIBRARIES}
    ${GST_VIDEO_LIBRARIES}
    ${CMAKE_DL_LIBS}
    ${GST_QCOM_VIDEO_LIBRARIES}
    ${GST_QCOM_UTILS_LIBRARIES}
    ${GST_QCOM_ALLOC_LIBRARIES}
  )

  install(
    TARGETS ${GST_DFS}
    LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
  )

endif()
