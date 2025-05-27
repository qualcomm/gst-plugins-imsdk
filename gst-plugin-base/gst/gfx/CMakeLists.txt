project(gstreamer-qcom-gfx-1.0
  VERSION 1.0.0
  LANGUAGES C CXX
  DESCRIPTION "GStreamer QTI Graphics base library"
)

set(TARGET_NAME gstqtigfxbase)

add_library(${TARGET_NAME} SHARED
  ib2c-gl-environment.cc
  ib2c-gl-engine.cc
  ib2c-gl-shader-program.cc
  ib2c-formats.cc
  ib2c-utils.cc
)

set_target_properties(${TARGET_NAME} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER ib2c.h
)

target_include_directories(${TARGET_NAME} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  EGL_EGLEXT_PROTOTYPES
  GL_GLEXT_PROTOTYPES
  $<$<BOOL:${HAVE_GBM_PRIV_H}>:HAVE_GBM_PRIV_H>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  EGL
  GLESv2
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/gfx/
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
