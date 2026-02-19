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
  ml-module-classification.c
  ml-module-detection.c
  ml-module-pose.c
  ml-module-segmentation.c
  ml-module-super-resolution.c
  ml-module-classification.c
  ml-post-process-classification.c
  ml-post-process-keypoint.c
  ml-post-process-detection.c
  ml-post-process-pose.c
)

set(TARGET_HEADERS
  ml-type.h
  ml-info.h
  ml-frame.h
  gstmlmeta.h
  gstmlmodule.h
  gstmlpool.h
  ml-module-utils.h
  ml-module-classification.h
  ml-module-detection.h
  ml-module-pose.h
  ml-module-segmentation.h
  ml-module-super-resolution.h
  ml-module-classification.h
  ml-post-process-classification.h
  ml-post-process-keypoint.h
  ml-post-process-detection.h
  ml-post-process-pose.h
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
  gstqtivideobase
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/ml/
)

# Add destination of compile time pkg-config file to PKG_CONFIG_LOCAL_PATH
list(APPEND PKG_CONFIG_LOCAL_PATH "${CMAKE_CURRENT_BINARY_DIR}")
set(PKG_CONFIG_LOCAL_PATH "${PKG_CONFIG_LOCAL_PATH}" CACHE STRING "" FORCE)

configure_file(
  ${QTI_PLUGINS_BASE_SOURCE_DIR}/common.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/lib/pkgconfig/${PROJECT_NAME}.pc
  @ONLY
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/lib/pkgconfig/${PROJECT_NAME}.pc
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
            --include=GstQtiVideo-1.0
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/utils
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/allocators
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/gfx
            --add-include-path=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/video
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
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-classification.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-classification.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-keypoint.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-keypoint.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-detection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-detection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-pose.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ml-post-process-pose.c
    DEPENDS ${TARGET_NAME}
            GstQtiVideo-1.0
  )

  set(TYPELIB_TARGET_NAME "GstQtiML-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME}
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/utils
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/allocators
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/gfx
            --includedir=${QTI_PLUGINS_BASE_BINARY_DIR}/gst/video
            --output=${TYPELIB_TARGET_NAME}
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
