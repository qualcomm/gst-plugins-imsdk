# GStreamer plugin.
set(GST_QTI_C2_ENGINE c2engine)

add_library(${GST_QTI_C2_ENGINE} SHARED
  c2-component.cc
  c2-wrapper.cc
  c2-config.cc
)

target_compile_definitions(${GST_QTI_C2_ENGINE} PRIVATE
  $<$<STREQUAL:${GST_QP_RANGES_VERSION},2.0>:QP_RANGES_VERSION_2_0>
)

target_include_directories(${GST_QTI_C2_ENGINE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_C2_ENGINE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_C2_ENGINE} PRIVATE
  ${GST_LIBRARIES}
  codec2_vndk
)

install(
  TARGETS ${GST_QTI_C2_ENGINE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
