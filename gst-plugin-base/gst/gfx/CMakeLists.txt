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

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
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
  set(GIR_TARGET_NAME "GstQtiGfx-1.0.gir")

  add_custom_command(
    OUTPUT ${GIR_TARGET_NAME}
    COMMAND ${GIR_SCANNER}
            --warn-all --no-libtool --verbose
            --namespace=GstQtiGfx
            --nsversion=1.0
            --identifier-prefix=GstGfx
            --identifier-prefix=Gst
            --symbol-prefix=gst_gfx
            --symbol-prefix=gst
            --library=${TARGET_NAME}
            --output=${GIR_TARGET_NAME}
            --include=Gst-1.0
    ${CMAKE_CURRENT_SOURCE_DIR}/gfx-utils.h
    DEPENDS ${TARGET_NAME}
  )

  set(TYPELIB_TARGET_NAME "GstQtiGfx-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME} --output=${TYPELIB_TARGET_NAME}
    DEPENDS ${GIR_TARGET_NAME}
  )

  add_custom_target(GstQtiGfx-1.0 ALL DEPENDS ${TYPELIB_TARGET_NAME})

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${GIR_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_DATADIR}/gir-1.0/
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${TYPELIB_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/girepository-1.0/
  )
endif()
