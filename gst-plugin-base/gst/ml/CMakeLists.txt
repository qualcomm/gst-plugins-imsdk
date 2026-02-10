project(gstreamer-qcom-oss-ml-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI ML base library"
)

set(TARGET_NAME gstqtimlbase)

add_library(${TARGET_NAME} SHARED
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
  ml-module-audio-classification.c
)

set(TARGET_HEADERS
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
  ml-module-audio-classification.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "${TARGET_HEADERS}"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  GST_ML_MODULES_DIR="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules"
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/ml/
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
  set(GIR_TARGET_NAME "GstQtiML-1.0.gir")

  add_custom_command(
    OUTPUT ${GIR_TARGET_NAME}
    COMMAND ${GIR_SCANNER}
            --warn-all --verbose
            --namespace=GstQtiML
            --nsversion=1.0
            --identifier-prefix=GstML
            --identifier-prefix=Gst
            --symbol-prefix=gst_ml
            --symbol-prefix=gst
            --library=${TARGET_NAME}
            --output=${GIR_TARGET_NAME}
            --include=Gst-1.0
            --include=GstVideo-1.0
            --cflags-begin -I${QTI_PLUGINS_BASE_SOURCE_DIR} --cflags-end
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-type.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-type.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-info.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-info.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-frame.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gstmlmeta.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstmlmeta.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gstmlpool.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstmlpool.c
    DEPENDS ${TARGET_NAME}
  )

  set(TYPELIB_TARGET_NAME "GstQtiML-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME} --output=${TYPELIB_TARGET_NAME}
    DEPENDS ${GIR_TARGET_NAME}
  )

  add_custom_target(GstQtiML-1.0 ALL DEPENDS ${TYPELIB_TARGET_NAME})

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${GIR_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_DATADIR}/gir-1.0/
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${TYPELIB_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/girepository-1.0/
  )
endif()
