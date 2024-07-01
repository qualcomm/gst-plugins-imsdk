# GStreamer QTI UTILS base library.
set(GST_QTI_UTILS_BASE gstqtiutilsbase)

add_library(${GST_QTI_UTILS_BASE} SHARED
  common-utils.c
  batch-utils.c
)

set_target_properties(${GST_QTI_UTILS_BASE} PROPERTIES PUBLIC_HEADER
  "common-utils.h;batch-utils.h"
)

target_include_directories(${GST_QTI_UTILS_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_UTILS_BASE} PRIVATE
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_UTILS_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/utils/
)
