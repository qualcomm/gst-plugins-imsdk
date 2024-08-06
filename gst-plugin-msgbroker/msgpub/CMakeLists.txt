# GStreamer plugin.

set(MSG_PUB gstqtimsgpub)

add_library(${MSG_PUB} SHARED
  msgpub.c
)

target_include_directories(${MSG_PUB} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${MSG_PUB} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  gstqtimsgadaptor
)

install(
  TARGETS ${MSG_PUB}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)