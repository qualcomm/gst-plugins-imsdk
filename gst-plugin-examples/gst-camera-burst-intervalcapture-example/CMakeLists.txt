# GStreamer camera burst interval capture-image example
set(GST_EXAMPLE_BIN gst-camera-burst-intervalcapture-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  $<$<BOOL:${HAVE_CAMX_API}>:${SYSROOT_INCDIR}/camx-api/camx/service>
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${CODEC2_ENCODE}>:CODEC2_ENCODE>
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  qmmf_camera_metadata
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
