cmake_minimum_required(VERSION 3.8.2)
project(GST_SAMPLE_APPS_UTILS_LIB LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

set(GST_SAMPLE_APPS_UTILS gstappsutils)

add_library(${GST_SAMPLE_APPS_UTILS} SHARED
  gst_sample_apps_utils.c
)

set_target_properties(${GST_SAMPLE_APPS_UTILS} PROPERTIES PUBLIC_HEADER
  "gst_sample_apps_utils.h;gst_sample_apps_pipeline.h"
)

target_include_directories(${GST_SAMPLE_APPS_UTILS} PUBLIC
  ${GST_INCLUDE_DIRS})

target_link_libraries(${GST_SAMPLE_APPS_UTILS} PRIVATE
  ${GST_LIBRARIES}
)
#LIBRARY DESTINATION /usr/lib/
#PUBLIC_HEADER DESTINATION /usr/include/gstreamer-1.0/gst/apps/
install(
  TARGETS ${GST_SAMPLE_APPS_UTILS}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/sampleapps/
)
