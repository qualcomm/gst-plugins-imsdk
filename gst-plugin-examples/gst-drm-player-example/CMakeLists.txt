# GStreamer DRM Player example
set(GST_EXAMPLE_BIN gst-drm-player-example)

add_executable(${GST_EXAMPLE_BIN}
  main.cc
)

pkg_check_modules (XML2 REQUIRED libxml-2.0)
pkg_check_modules (CURL REQUIRED libcurl)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}
  ${XML2_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  dl
  utils
  cutils
  ${GST_LIBRARIES}
  ${CURL_LIBRARIES}
  ${XML2_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
