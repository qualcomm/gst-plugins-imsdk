set(GST_MLE_LIBRARY Engine_MLE)

list(APPEND SOURCE_FILES "ml_engine_impl.cc")

if (TFLITE_ENABLE)
  add_definitions(-DTFLITE_ENABLE)
  list(APPEND SOURCE_FILES "tflite_base.cc")
  list(APPEND SOURCE_FILES "tflite_segmentation.cc")
  list(APPEND SOURCE_FILES "tflite_posenet.cc")
endif()

if (SNPE_ENABLE)
  add_definitions(-DSNPE_ENABLE)
  list(APPEND SOURCE_FILES "snpe_base.cc")
  list(APPEND SOURCE_FILES "snpe_detection.cc")
  list(APPEND SOURCE_FILES "snpe_single_ssd.cc")
  list(APPEND SOURCE_FILES "snpe_segmentation.cc")
endif()

add_library(${GST_MLE_LIBRARY} SHARED
  ${SOURCE_FILES}
)

target_include_directories(${GST_MLE_LIBRARY} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}/ion_headers
)

target_include_directories(${GST_MLE_LIBRARY} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
  ${SYSROOT_INCDIR}/ion_headers
)

target_link_libraries(${GST_MLE_LIBRARY} PRIVATE
  log
  dl
  cutils
  fastcvopt
)

if (SNPE_ENABLE)
  target_link_libraries(${GST_MLE_LIBRARY} PRIVATE SNPE)
endif()

if (TFLITE_ENABLE)
  target_link_libraries(${GST_MLE_LIBRARY} PRIVATE tensorflow-lite)
endif()

install(
  TARGETS ${GST_MLE_LIBRARY}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_ENGINEDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)
