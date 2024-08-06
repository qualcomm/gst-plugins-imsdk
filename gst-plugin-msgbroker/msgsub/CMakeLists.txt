# GStreamer plugin.

set(MSG_SUB gstqtimsgsub)

add_library(${MSG_SUB} SHARED
  msgsub.c
)

target_include_directories(${MSG_SUB} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${MSG_SUB} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  gstqtimsgadaptor
)

install(
  TARGETS ${MSG_SUB}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_READ OWNER_WRITE
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)