cmake_minimum_required(VERSION 3.8.2)
project(GST-WEBRTC-SENDRECV-EXAMPLE LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})
link_directories(${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_JSON
  REQUIRED json-glib-1.0)
pkg_check_modules(GST_SOUP
  REQUIRED libsoup-2.4)
pkg_check_modules(GST_WEBRTC
  REQUIRED gstreamer-webrtc-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_SDP
  REQUIRED gstreamer-sdp-1.0>=${GST_VERSION_REQUIRED})

# GStreamer gst-webrtc-sendrecv-example
set(GST_EXAMPLE_BIN gst-webrtc-sendrecv-example)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${GST_JSON_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_JSON_LIBRARIES}
  ${GST_SOUP_LIBRARIES}
  ${GST_WEBRTC_LIBRARIES}
  ${GST_SDP_LIBRARIES}
  gstapp-1.0
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)