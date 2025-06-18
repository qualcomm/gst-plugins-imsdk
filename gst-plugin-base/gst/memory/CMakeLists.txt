project(gstreamer-qcom-oss-memory-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI Memory base library"
)

set(TARGET_NAME gstqtimemorybase)

add_library(${TARGET_NAME} SHARED
  gstmempool.c
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "gstmempool.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
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

target_compile_definitions(${TARGET_NAME} PRIVATE
  $<$<BOOL:${HAVE_LINUX_DMA_HEAP_H}>:HAVE_LINUX_DMA_HEAP_H>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/memory/
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
