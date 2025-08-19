set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(TARGET_NAME ml-postprocess-deeplab-argmax)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-deeplab-argmax.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-yolov8-seg)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-yolov8-seg.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-midas-v2)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-midas-v2.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
