cmake_minimum_required(VERSION 3.8.2)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_ALLOC
  REQUIRED gstreamer-allocators-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

# GStreamer gst-buffering-encoding-mode-switch-example
set(GST_EXAMPLE_BIN gst-buffering-encoding-mode-switch-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${CODEC2_ENCODE}>:CODEC2_ENCODE>
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  pthread
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
