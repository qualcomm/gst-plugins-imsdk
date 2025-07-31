project(gstreamer-qcom-oss-video-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI Video base library"
)

set(TARGET_NAME gstqtivideobase)

add_library(${TARGET_NAME} SHARED
  gstimagepool.c
  gstvideoclassificationmeta.c
  gstvideolandmarksmeta.c
  video-converter-engine.c
  video-utils.c
  $<$<BOOL:${HAVE_ADRENO_C2D2_H}>:c2d-video-converter.c>
  $<$<BOOL:${GLES_FOUND}>:gles-video-converter.cc>
  $<$<BOOL:${HAVE_FASTCV_H}>:fcv-video-converter.c>
)

set(TARGET_HEADERS
  gstimagepool.h
  gstvideoclassificationmeta.h
  gstvideolandmarksmeta.h
  video-converter-engine.h
  video-utils.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "${TARGET_HEADERS}"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  $<$<BOOL:${HAVE_MMM_COLOR_FMT_H}>:HAVE_MMM_COLOR_FMT_H>
  $<$<BOOL:${HAVE_MSM_MEDIA_INFO_H}>:HAVE_MSM_MEDIA_INFO_H>
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
  $<$<BOOL:${HAVE_ADRENO_C2D2_H}>:HAVE_ADRENO_C2D2_H>
  $<$<BOOL:${GLES_FOUND}>:HAVE_GLES2_H>
  $<$<BOOL:${HAVE_FASTCV_H}>:HAVE_FASTCV_H>
  $<$<BOOL:${HAVE_GBM_PRIV_H}>:HAVE_GBM_PRIV_H>
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${TARGET_NAME} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

# Path for some headers in msm-5.4 kernel.
target_include_directories(${TARGET_NAME} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/vidc
)

# Path for some headers in msm-5.15 kernel.
target_include_directories(${TARGET_NAME} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include/uapi
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiallocatorsbase
  gstqtigfxbase
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/video/
)

configure_file(
  ${CMAKE_SOURCE_DIR}/gst/common.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  @ONLY
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/pkgconfig
)
