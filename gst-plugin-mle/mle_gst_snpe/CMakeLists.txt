# GStreamer plugin.
set(GST_MLE_SNPE gstqtimlesnpe)

add_library(${GST_MLE_SNPE} SHARED
  mle_snpe.cc
)

target_include_directories(${GST_MLE_SNPE} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_MLE_SNPE} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
  ${SYSROOT_INCDIR}/ion_headers
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${GST_MLE_SNPE} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  jsoncpp
  qtimlmeta
  Engine_MLE
)

install(
  TARGETS ${GST_MLE_SNPE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)

install(
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/mle_snpe_config.json
  DESTINATION /data/misc/camera
  PERMISSIONS OWNER_WRITE OWNER_READ
  GROUP_READ
)
