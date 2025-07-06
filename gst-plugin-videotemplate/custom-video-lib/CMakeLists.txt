set(GST_QTI_VIDEO_TEMPLATE_SIMPLE custom-video-lib-simple)

add_library(${GST_QTI_VIDEO_TEMPLATE_SIMPLE} SHARED
  custom-video-lib-common.c
  custom-video-lib-simple.c
)

target_include_directories(${GST_QTI_VIDEO_TEMPLATE_SIMPLE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_VIDEO_TEMPLATE_SIMPLE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
  gstqtivideotemplatedefs
)

install(
  TARGETS ${GST_QTI_VIDEO_TEMPLATE_SIMPLE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_QTI_VIDEO_TEMPLATE_ASYNC custom-video-lib-async)

add_library(${GST_QTI_VIDEO_TEMPLATE_ASYNC} SHARED
  custom-video-lib-async.c
  custom-video-lib-common.c
)

target_include_directories(${GST_QTI_VIDEO_TEMPLATE_ASYNC} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_VIDEO_TEMPLATE_ASYNC} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  gstqtiutilsbase
  gstqtivideobase
  gstqtivideotemplatedefs
)

install(
  TARGETS ${GST_QTI_VIDEO_TEMPLATE_ASYNC}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
