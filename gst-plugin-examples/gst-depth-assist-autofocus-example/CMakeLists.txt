# GStreamer depth-assist-autofocus-example example
set(GST_EXAMPLE_BIN gst-depth-assist-autofocus-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${CAMERA_CLIENT_DISABLED}>:camera_metadata>
  $<$<NOT:$<BOOL:${CAMERA_CLIENT_DISABLED}>>:camera_client>
  $<$<NOT:$<BOOL:${CAMERA_CLIENT_DISABLED}>>:binder>
  $<$<NOT:$<BOOL:${CAMERA_CLIENT_DISABLED}>>:cutils>
  ${GST_LIBRARIES}
  utils
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
