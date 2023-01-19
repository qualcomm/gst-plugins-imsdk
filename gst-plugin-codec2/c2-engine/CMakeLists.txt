# GStreamer plugin.
set(GST_QTI_C2_ENGINE c2engine)

add_library(${GST_QTI_C2_ENGINE} SHARED
  c2-engine.cc
  c2-engine-params.cc
  c2-engine-utils.cc
  c2-component.cc
)

target_compile_definitions(${GST_QTI_C2_ENGINE} PRIVATE
  $<$<STREQUAL:${GST_CODEC2_CONFIG_VERSION},2.0>:CODEC2_CONFIG_VERSION_2_0>
  $<$<BOOL:${HAVE_MMM_COLOR_FMT_H}>:HAVE_MMM_COLOR_FMT_H>
)

target_include_directories(${GST_QTI_C2_ENGINE} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${KERNEL_BUILDDIR}/usr/include
)

# Headers path for msm-5.4 kernel.
target_include_directories(${GST_QTI_C2_ENGINE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/vidc
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
