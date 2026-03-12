cmake_minimum_required(VERSION 3.16)
project(GST_UVC_CAMERA_DAEMON LANGUAGES C)

find_package(PkgConfig)
find_package(ALSA REQUIRED)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# GStreamer UVC Camera daemon
set(GST_DAEMON_BIN gst-umd-daemon)

add_executable(${GST_DAEMON_BIN}
  main.c
)

target_include_directories(${GST_DAEMON_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(${GST_DAEMON_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  qtiumd
  dl
  cutils
  ${ALSA_LIBRARIES}
)

install(
  TARGETS ${GST_DAEMON_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/uvc.config
  DESTINATION /data
  PERMISSIONS OWNER_WRITE OWNER_READ
              GROUP_READ
              WORLD_READ
)
