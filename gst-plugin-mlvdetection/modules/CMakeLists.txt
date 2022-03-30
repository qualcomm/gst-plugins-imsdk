# GStreamer ML video detection plugin modules.
set(GST_QTI_ML_MODULE ssd-mobilenet-tflite)

add_library(${GST_QTI_ML_MODULE} SHARED
  ssd-mobilenet-tflite.c
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

set(GST_QTI_ML_MODULE ssd-mobilenet-dlc)

add_library(${GST_QTI_ML_MODULE} SHARED
  ssd-mobilenet-dlc.c
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

if(GST_AIC_SMARTNMS)
  set(GST_QTI_ML_MODULE ssd-yolo)

  add_library(${GST_QTI_ML_MODULE} SHARED
    ssd-yolo.cc
  )

  target_include_directories(${GST_QTI_ML_MODULE} PUBLIC
    ${GST_INCLUDE_DIRS}
  )

  target_link_libraries(${GST_QTI_ML_MODULE} PRIVATE
    ${GST_LIBRARIES}
    gstqtimlbase
    AICsmartnms
  )

  install(
    TARGETS ${GST_QTI_ML_MODULE}
    LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
  )

  install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/configs/user-config-yolov5m.yml
    DESTINATION /data/misc/camera
    PERMISSIONS OWNER_WRITE OWNER_READ
    GROUP_READ
  )
endif()

set(GST_QTI_ML_MODULE yolov5m)

add_library(${GST_QTI_ML_MODULE} SHARED
  yolov5m.c
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
