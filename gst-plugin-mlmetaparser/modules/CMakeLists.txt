
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(JSON
  REQUIRED json-glib-1.0)

# GStreamer ML meta parser plugin modules.
set(GST_QTI_PARSER_MODULE ml-meta-parser-json)

add_library(${GST_QTI_PARSER_MODULE} SHARED
  ml-meta-parser-json.c
)

target_include_directories(${GST_QTI_PARSER_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${JSON_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_PARSER_MODULE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${JSON_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_PARSER_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/mlmetaparser/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)