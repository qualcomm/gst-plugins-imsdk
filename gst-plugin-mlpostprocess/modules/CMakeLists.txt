cmake_minimum_required(VERSION 3.8.2)
project(QTI_OSS_ML_MODULES LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Common compiler flags.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")

set(TARGET_NAME ml-postprocess-yolov8)

add_library(${TARGET_NAME} SHARED
  object-detection/ml-postprocess-yolov8.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-mobilenet-softmax)

add_library(${TARGET_NAME} SHARED
  image-classification/ml-postprocess-mobilenet-softmax.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-posenet)

add_library(${TARGET_NAME} SHARED
  pose-estimation/ml-postprocess-posenet.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-deeplab-argmax)

add_library(${TARGET_NAME} SHARED
  image-segmentation/ml-postprocess-deeplab-argmax.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-srnet)

add_library(${TARGET_NAME} SHARED
  super-resolution/ml-postprocess-srnet.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(TARGET_NAME ml-postprocess-yamnet)

add_library(${TARGET_NAME} SHARED
  audio-classification/ml-postprocess-yamnet.cc
)

install(
  TARGETS ${TARGET_NAME}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0/ml/modules
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
