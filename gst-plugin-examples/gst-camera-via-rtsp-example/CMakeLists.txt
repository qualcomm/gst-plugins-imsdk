cmake_minimum_required(VERSION 3.16)

# GStreamer gst-camera-via-rtsp-example
set(GST_EXAMPLE_BIN gst-camera-via-rtsp-example)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${GST_RTSP_SERVER_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_RTSP_SERVER_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
