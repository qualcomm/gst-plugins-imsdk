# GStreamer plugins pipeline test example.
project(GST_PLUGIN_QTI_OSS_HIBERNATE
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

# This calls pkgconfig with appropriate arguments.
pkg_check_modules(DBUS REQUIRED dbus-1)

set(GST_PIPELINE_EXECUTABLE gst-hibernate-example)

add_executable(${GST_PIPELINE_EXECUTABLE}
  main.c
  wifi.cc
)

target_include_directories(${GST_PIPELINE_EXECUTABLE} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${DBUS_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}/dsutils
  ${SYSROOT_INCDIR}/qmi
  ${SYSROOT_INCDIR}/qmi-framework
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_PIPELINE_EXECUTABLE} PRIVATE
  ${GST_LIBRARIES}
  ${DBUS_LIBRARIES}
  cutils
  libqcmap_client.so
)

install(
  TARGETS ${GST_PIPELINE_EXECUTABLE}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
