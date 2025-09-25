set(CMAKE_INCLUDE_CURRENT_DIR ON)
pkg_check_modules(OPEN_CV REQUIRED opencv4)

set(TARGET_NAME ml-postprocess-yolov8)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-yolov8.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-ssd-mobilenet)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-ssd-mobilenet.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-yolo-nas)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-yolo-nas.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-east-textdt)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-east-textdt.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-qfd)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-qfd.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-yolov5)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-yolov5.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-qpd)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-qpd.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-easy-ocr-detector)

add_library(${TARGET_NAME} SHARED
  ml-postprocess-easy-ocr-detector.cc
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${OPEN_CV_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME} PRIVATE
  ${OPEN_CV_LIBRARIES}
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
