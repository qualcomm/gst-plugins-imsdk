# GStreamer ML video detection plugin modules.
set(GST_QTI_ML_MODULE ml-vdetection-ssd-mobilenet)

add_library(${GST_QTI_ML_MODULE} SHARED
  ml-vdetection-ssd-mobilenet.c
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_QTI_ML_MODULE ml-vdetection-yolov5m)

add_library(${GST_QTI_ML_MODULE} SHARED
  ml-vdetection-yolov5m.c
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
            GROUP_EXECUTE GROUP_READ
            WORLD_EXECUTE WORLD_READ
)

set(GST_QTI_ML_MODULE ml-vdetection-face-detect)

add_library(${GST_QTI_ML_MODULE} SHARED
  ml-vdetection-face-detect.c
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
            GROUP_EXECUTE GROUP_READ
            WORLD_EXECUTE WORLD_READ
)

set(GST_QTI_ML_MODULE ml-vdetection-yolov5s)

add_library(${GST_QTI_ML_MODULE} SHARED
  ml-vdetection-yolov5s.c
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
            GROUP_EXECUTE GROUP_READ
            WORLD_EXECUTE WORLD_READ
)

#YOLO-NAS
set(GST_QTI_ML_MODULE ml-vdetection-yolo-nas)

add_library(${GST_QTI_ML_MODULE} SHARED
  ml-vdetection-yolo-nas.c
)

target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
  ${GST_LIBRARIES}
  gstqtimlbase
)

install(
  TARGETS ${GST_QTI_ML_MODULE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
            GROUP_EXECUTE GROUP_READ
            WORLD_EXECUTE WORLD_READ
)
