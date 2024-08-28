pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

set(GST_QTI_META_MODULE meta-transform-roi-label-moving-average)

add_library(${GST_QTI_META_MODULE} SHARED
  meta-transform-roi-label-moving-average.c
)

target_include_directories(${GST_QTI_META_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_META_MODULE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
)

install(
  TARGETS ${GST_QTI_META_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/meta/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
