project(gstreamer-qcom-oss-utils-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI Utils base library"
)

set(TARGET_NAME gstqtiutilsbase)

set(TARGET_HEADERS
  common-utils.h
  batch-utils.h
  gsttextmeta.h
)

add_library(${TARGET_NAME} SHARED
  common-utils.c
  batch-utils.c
  gsttextmeta.c
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "${TARGET_HEADERS}"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${GST_JSON_INCLUDE_DIRS}
)

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_JSON_LIBRARIES}
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/utils/
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
  set(GIR_TARGET_NAME "GstQtiCommon-1.0.gir")

  add_custom_command(
    OUTPUT ${GIR_TARGET_NAME}
    COMMAND ${GIR_SCANNER}
            --warn-all --verbose
            --namespace=GstQtiCommon
            --nsversion=1.0
            --identifier-prefix=Gst
            --symbol-prefix=gst
            --library=${TARGET_NAME}
            --output=${GIR_TARGET_NAME}
            --include=Gst-1.0
    ${CMAKE_CURRENT_SOURCE_DIR}/common-utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common-utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/batch-utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/batch-utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gsttextmeta.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gsttextmeta.c
    DEPENDS ${TARGET_NAME}
  )

  set(TYPELIB_TARGET_NAME "GstQtiCommon-1.0.typelib")

  add_custom_command(
    OUTPUT ${TYPELIB_TARGET_NAME}
    COMMAND ${GIR_COMPILER} ${GIR_TARGET_NAME} --output=${TYPELIB_TARGET_NAME}
    DEPENDS ${GIR_TARGET_NAME}
  )

  add_custom_target(GstQtiCommon-1.0 ALL DEPENDS ${TYPELIB_TARGET_NAME})

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${GIR_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_DATADIR}/gir-1.0/
  )

  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${TYPELIB_TARGET_NAME}
    DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/girepository-1.0/
  )
endif()
