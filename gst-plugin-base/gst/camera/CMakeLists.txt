project(gstreamer-qcom-oss-camera-meta-1.0
  VERSION 1.0.0
  LANGUAGES C
  DESCRIPTION "GStreamer QTI Camera Meta base library"
)

set(TARGET_NAME gstqticamerabase)

add_library(${TARGET_NAME} SHARED ${ENABLE_EXCLUDE_FROM_ALL}
  gstcamerameta.cc
)

set_target_properties(${TARGET_NAME} PROPERTIES
  PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/gstcamerameta.h"
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}/camx-api/camx/service
)

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${QTI_PLUGINS_BASE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/gstreamer-1.0>
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  qmmf_camera_metadata
)

install(
  FILES
    $<TARGET_FILE:${TARGET_NAME}>
    $<TARGET_SONAME_FILE:${TARGET_NAME}>
    $<TARGET_LINKER_FILE:${TARGET_NAME}>
  DESTINATION ${CMAKE_INSTALL_LIBDIR}
  OPTIONAL
)

install(CODE "
  if(EXISTS \"$<TARGET_FILE:${TARGET_NAME}>\")
    file(INSTALL
      DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/gstreamer-1.0/gst/camera/\"
      TYPE FILE
      FILES \"$<TARGET_PROPERTY:${TARGET_NAME},PUBLIC_HEADER>\"
    )
  endif()
")

# Add destination of compiletime pkg-config file to PKG_CONFIG_PATH
set(ENV{PKG_CONFIG_PATH} "${CMAKE_CURRENT_BINARY_DIR}:$ENV{PKG_CONFIG_PATH}")

configure_file(
  ${BASE_COMMON_PC_IN}
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  @ONLY
)

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc
  DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/pkgconfig
)
