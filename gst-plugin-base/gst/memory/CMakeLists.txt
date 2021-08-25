# GStreamer QTI Memory base library.
set(GST_QTI_MEMORY_BASE gstqtimemorybase)

add_library(${GST_QTI_MEMORY_BASE} SHARED
  gstionpool.c
)

set_target_properties(${GST_QTI_MEMORY_BASE} PROPERTIES PUBLIC_HEADER
  gstionpool.h
)

target_include_directories(${GST_QTI_MEMORY_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_MEMORY_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

# Headers path for msm-5.4 media on qrbx210 targets.
target_include_directories(${GST_QTI_MEMORY_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/vidc
)

target_link_libraries(${GST_QTI_MEMORY_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_MEMORY_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/memory/
)
