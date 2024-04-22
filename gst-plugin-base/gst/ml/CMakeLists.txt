# GStreamer QTI ML base library.
set(GST_QTI_ML_BASE gstqtimlbase)

add_library(${GST_QTI_ML_BASE} SHARED
  ml-type.c
  ml-info.c
  ml-frame.c
  gstmlmeta.c
  gstmlmodule.c
  gstmlpool.c
  ml-module-utils.c
  ml-module-video-classification.c
  ml-module-video-detection.c
  ml-module-video-pose.c
  ml-module-video-segmentation.c
  ml-module-video-super-resolution.c
)

set(GST_QTI_ML_BASE_HEADERS
  ml-type.h
  ml-info.h
  ml-frame.h
  gstmlmeta.h
  gstmlmodule.h
  gstmlpool.h
  ml-module-utils.h
  ml-module-video-classification.h
  ml-module-video-detection.h
  ml-module-video-pose.h
  ml-module-video-segmentation.h
  ml-module-video-super-resolution.h
)

set_target_properties(${GST_QTI_ML_BASE} PROPERTIES PUBLIC_HEADER
  "${GST_QTI_ML_BASE_HEADERS}"
)

target_include_directories(${GST_QTI_ML_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_ML_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

# Path for some headers in msm-5.4 kernel.
target_include_directories(${GST_QTI_ML_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/vidc
)

# Path for some headers in msm-5.15 kernel.
target_include_directories(${GST_QTI_ML_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/uapi
)

target_compile_definitions(${GST_QTI_ML_BASE} PRIVATE
  GST_ML_MODULES_DIR="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules"
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
)

target_link_libraries(${GST_QTI_ML_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_ML_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/ml/
)
