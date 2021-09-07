# GStreamer QTI Video base library.
set(GST_QTI_VIDEO_BASE gstqtivideobase)

add_library(${GST_QTI_VIDEO_BASE} SHARED
  gstimagepool.c
  c2d-video-converter.c
)

set_target_properties(${GST_QTI_VIDEO_BASE} PROPERTIES PUBLIC_HEADER
  "gstimagepool.h;c2d-video-converter.h"
)

target_include_directories(${GST_QTI_VIDEO_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_VIDEO_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_VIDEO_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_VIDEO_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/video/
)
