# GStreamer app source exmaple
set(GST_EXAMPLE_BIN gst-gbm-appsrc-example)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtivideobase
)

target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${CODEC2_ENCODE}>:CODEC2_ENCODE>
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)


