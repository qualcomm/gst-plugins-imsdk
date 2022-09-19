# GStreamer camera-metadata-example example
set(GST_EXAMPLE_BIN gst-camera-metadata-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

if(CAMERA_CLIENT_DISABLED)
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  camera_metadata
)
else()
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  camera_client
)
endif()

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  binder
  cutils
  utils
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
