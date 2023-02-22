# GStreamer QTI Video base library.
set(GST_QTI_VIDEO_BASE gstqtivideobase)

add_library(${GST_QTI_VIDEO_BASE} SHARED
  gstimagepool.c
  $<$<BOOL:${GST_C2D_CONVERTER}>:c2d-video-converter.c>
  $<$<BOOL:${GST_GLES_CONVERTER}>:gles-video-converter.cc>
)

set(GST_QTI_VIDEO_BASE_HEADERS
  gstimagepool.h
  $<$<BOOL:${GST_C2D_CONVERTER}>:${CMAKE_CURRENT_SOURCE_DIR}/c2d-video-converter.h>
  $<$<BOOL:${GST_GLES_CONVERTER}>:${CMAKE_CURRENT_SOURCE_DIR}/gles-video-converter.h>
)

set_target_properties(${GST_QTI_VIDEO_BASE} PROPERTIES PUBLIC_HEADER
  "${GST_QTI_VIDEO_BASE_HEADERS}"
)

target_compile_definitions(${GST_QTI_VIDEO_BASE} PRIVATE
  $<$<BOOL:${HAVE_MMM_COLOR_FMT_H}>:HAVE_MMM_COLOR_FMT_H>
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
  # TODO: set flag based on GBM version
  $<$<BOOL:${GBM_FREE_FD}>:GBM_FREE_FD>
)

target_include_directories(${GST_QTI_VIDEO_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_VIDEO_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

# Path for some headers in msm-5.4 kernel.
target_include_directories(${GST_QTI_VIDEO_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/vidc
)

# Path for some headers in msm-5.15 kernel.
target_include_directories(${GST_QTI_VIDEO_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/uapi
)

target_link_libraries(${GST_QTI_VIDEO_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_VIDEO_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/video/
)
