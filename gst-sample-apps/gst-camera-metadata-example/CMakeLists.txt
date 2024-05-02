# GStreamer camera-metadata-example example
set(GST_EXAMPLE_BIN gst-camera-metadata-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  $<$<STREQUAL:${CAMERA_SERVICE},LECAM>:${SYSROOT_INCDIR}/camx-api/camx/service>
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  qmmf_camera_metadata
  camera_metadata
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

