# GStreamer ML video segmentation plugin modules.
set(GST_QTI_ML_MODULE deeplab-argmax-tflite)

add_library(${GST_QTI_ML_MODULE} SHARED
  deeplab-argmax-tflite.c
)

target_compile_definitions(${GST_QTI_ML_MODULE} PRIVATE
  $<$<BOOL:${HAVE_LINUX_DMA_BUF_H}>:HAVE_LINUX_DMA_BUF_H>
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
