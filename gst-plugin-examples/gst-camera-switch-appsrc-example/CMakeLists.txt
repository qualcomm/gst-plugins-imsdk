# GStreamer gst-camera-switch-appsrc-example
set(GST_EXAMPLE_BIN gst-camera-switch-appsrc-example)

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
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  gstapp-1.0
  gstqtivideobase
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)