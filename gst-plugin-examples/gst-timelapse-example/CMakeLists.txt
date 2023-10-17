cmake_minimum_required(VERSION 3.8.2)

# GStreamer gst-timelapse-example
set(GST_EXAMPLE_BIN gst-timelapse-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<STREQUAL:${CAMERA_METADATA_VERSION},1.0ns>:CAMERA_METADATA_1_0_NS>
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)