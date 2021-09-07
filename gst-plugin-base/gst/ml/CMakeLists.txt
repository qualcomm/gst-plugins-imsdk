# GStreamer QTI ML base library.
set(GST_QTI_ML_BASE gstqtimlbase)

add_library(${GST_QTI_ML_BASE} SHARED
  ml-type.c
  ml-info.c
  gstmlmeta.c
  gstmlpool.c
)

set_target_properties(${GST_QTI_ML_BASE} PROPERTIES PUBLIC_HEADER
  "ml-type.h;ml-info.h;gstmlmeta.h;gstmlpool.h"
)

target_include_directories(${GST_QTI_ML_BASE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_ML_BASE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
)

target_link_libraries(${GST_QTI_ML_BASE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
)

install(
  TARGETS ${GST_QTI_ML_BASE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  PUBLIC_HEADER DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_INCDIR}/gstreamer-1.0/gst/ml/
)
