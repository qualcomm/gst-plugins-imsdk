# GStreamer depth-assist-autofocus-example example
set(GST_EXAMPLE_BIN gst-depth-assist-autofocus-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
   $<$<BOOL:${HAVE_CAMX_API}>:${SYSROOT_INCDIR}/camx-api/camx/service>
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  qmmf_camera_metadata
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
