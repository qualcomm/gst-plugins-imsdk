project(gstreamer-qcom-oss-allocators-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI Allocator base library"
)

set(TARGET_NAME gstqtiallocatorsbase)

add_library(${TARGET_NAME} SHARED
  gstqtiallocator.c
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "gstqtiallocator.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/allocators/
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
  set(GIR_TARGET_NAME "GstQtiAllocators-1.0.gir")

  add_custom_command(
    OUTPUT ${GIR_TARGET_NAME}
    COMMAND ${GIR_SCANNER}
            --warn-all --verbose
            --namespace=GstQtiAllocators
            --nsversion=1.0
            --identifier-prefix=GstQti
            --identifier-prefix=Gst
            --symbol-prefix=gst_qti
            --symbol-prefix=gst
            --library=${TARGET_NAME}
            --output=${GIR_TARGET_NAME}
            --include=Gst-1.0
            --include=GstBase-1.0
            --include=GstAllocators-1.0
    ${CMAKE_CURRENT_SOURCE_DIR}/gstqtiallocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gstqtiallocator.c
    DEPENDS ${TARGET_NAME}
  )

  set(TYPELIB_TARGET_NAME "GstQtiAllocators-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME} --output=${TYPELIB_TARGET_NAME}
    DEPENDS ${GIR_TARGET_NAME}
  )

  add_custom_target(GstQtiAllocators-1.0 ALL DEPENDS ${TYPELIB_TARGET_NAME})

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${GIR_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_DATADIR}/gir-1.0/
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${TYPELIB_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/girepository-1.0/
  )
endif()
