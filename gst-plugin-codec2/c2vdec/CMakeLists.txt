# GStreamer plugin.
set(GST_QTI_C2_VDEC_DEC gstqtic2vdec)

add_library(${GST_QTI_C2_VDEC_DEC} SHARED
  gstc2vdec.cc
)

target_include_directories(${GST_QTI_C2_VDEC_DEC} PUBLIC
  ${GST_INCLUDE_DIRS}
)

target_include_directories(${GST_QTI_C2_VDEC_DEC} PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${GST_QTI_C2_VDEC_DEC} PRIVATE
  ${GST_LIBRARIES}
  ${GST_ALLOC_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  codec2_vndk
  c2engine
)

install(
  TARGETS ${GST_QTI_C2_VDEC_DEC}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}/gstreamer-1.0
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)
