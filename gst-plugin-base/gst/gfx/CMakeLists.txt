project(gstreamer-qcom-gfx-1.0
  VERSION 1.0.0
  LANGUAGES C CXX
  DESCRIPTION "GStreamer QTI Graphics base library"
)

set(TARGET_NAME gstqtigfxbase)

add_library(${TARGET_NAME} SHARED
  $<$<BOOL:${GLES_FOUND}>:ib2c-gl-environment.cc>
  $<$<BOOL:${GLES_FOUND}>:ib2c-gl-engine.cc>
  $<$<BOOL:${GLES_FOUND}>:ib2c-gl-shader-program.cc>
  $<$<BOOL:${GLES_FOUND}>:ib2c-formats.cc>
  $<$<BOOL:${GLES_FOUND}>:ib2c-utils.cc>
  gfx-utils.cc
)

set(TARGET_HEADERS
  $<$<BOOL:${GLES_FOUND}>:${CMAKE_CURRENT_SOURCE_DIR}/ib2c.h>
  gfx-utils.h
)

set_target_properties(${TARGET_NAME} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  PUBLIC_HEADER "${TARGET_HEADERS}"
)

target_include_directories(${TARGET_NAME} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
  $<$<BOOL:${GLES_FOUND}>:HAVE_GLES2_H>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
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
