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
  $<$<BOOL:${OPEN_CV_FOUND}>:ocv-video-converter.cc>
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

if (FASTCV_FOUND)
  string(REGEX MATCH "^[0-9]+" FASTCV_VERSION_MAJOR ${FASTCV_VERSION})
endif()

target_compile_definitions(${TARGET_NAME} PRIVATE
  $<$<BOOL:${HAVE_MMM_COLOR_FMT_H}>:HAVE_MMM_COLOR_FMT_H>
  $<$<BOOL:${HAVE_MSM_MEDIA_INFO_H}>:HAVE_MSM_MEDIA_INFO_H>
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
  $<$<BOOL:${HAVE_ADRENO_C2D2_H}>:HAVE_ADRENO_C2D2_H>
  $<$<BOOL:${GLES_FOUND}>:HAVE_GLES2_H>
  $<$<BOOL:${HAVE_FASTCV_H}>:HAVE_FASTCV_H>
  $<$<BOOL:${FASTCV_FOUND}>:FASTCV_PKG_FOUND>
  $<$<BOOL:${FASTCV_FOUND}>:FASTCV_VERSION_MAJOR="${FASTCV_VERSION_MAJOR}">
  $<$<BOOL:${OPEN_CV_FOUND}>:HAVE_OPENCV_H>
  $<$<BOOL:${HAVE_GBM_PRIV_H}>:HAVE_GBM_PRIV_H>
)

target_include_directories(${TARGET_NAME} PRIVATE
  ${GST_INCLUDE_DIRS}
  $<$<BOOL:${OPEN_CV_FOUND}>:${OPEN_CV_INCLUDE_DIRS}>
)

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiallocatorsbase
  gstqtigfxbase
  gstqtiutilsbase
  $<$<BOOL:${OPEN_CV_FOUND}>:${OPEN_CV_LIBRARIES}>
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/video/
)

# Add destination of compiletime pkg-config file to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${CMAKE_CURRENT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

configure_file(
  ${QTI_PLUGINS_BASE_SOURCE_DIR}/common.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  @ONLY
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/pkgconfig
)

if(GIR_SCANNER AND GIR_COMPILER AND PYTHON_SITEPACKAGES_DIR)
  # Generate GIR and typelib files for Python
  set(GIR_TARGET_NAME "GstQtiVideo-1.0.gir")

  add_custom_command(
    OUTPUT ${GIR_TARGET_NAME}
    COMMAND ${GIR_SCANNER}
            --warn-all --verbose
            --namespace=GstQtiVideo
            --nsversion=1.0
            --identifier-prefix=GstVideo
            --identifier-prefix=Gst
            --symbol-prefix=gst_video
            --symbol-prefix=gst
            --library=${TARGET_NAME}
            --output=${GIR_TARGET_NAME}
            --include=Gst-1.0
            --include=GstAllocators-1.0
            --include=GstVideo-1.0
            --include=GstQtiAllocators-1.0
            --include=GstQtiGfx-1.0
            --include=GstQtiCommon-1.0
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/utils
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/allocators
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/gfx
            --cflags-begin -I${QTI_PLUGINS_BASE_SOURCE_DIR} --cflags-end
    ${CMAKE_CURRENT_SOURCE_DIR}/video-utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/video-utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/video-converter-engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/video-converter-engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gstvideoclassificationmeta.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstvideoclassificationmeta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gstvideolandmarksmeta.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstvideolandmarksmeta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gstimagepool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstimagepool.c
    DEPENDS ${TARGET_NAME}
            GstQtiAllocators-1.0
            GstQtiGfx-1.0
            GstQtiCommon-1.0
  )

  set(TYPELIB_TARGET_NAME "GstQtiVideo-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME}
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/utils
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/allocators
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/gfx
            --output=${TYPELIB_TARGET_NAME}
    DEPENDS ${GIR_TARGET_NAME}
  )

  add_custom_target(GstQtiVideo-1.0 ALL DEPENDS ${TYPELIB_TARGET_NAME})

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${GIR_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_DATADIR}/gir-1.0/
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${TYPELIB_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/girepository-1.0/
  )
endif()
