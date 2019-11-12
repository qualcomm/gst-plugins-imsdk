set(GST_MLE_LIBRARY Engine_MLE)

list(APPEND SOURCE_FILES "tflite_base.cc")

if (SNPE_ENABLE)
  add_definitions(-DSNPE_ENABLE)
  list(APPEND SOURCE_FILES "snpe_base.cc")
  list(APPEND SOURCE_FILES "snpe_complex.cc")
  list(APPEND SOURCE_FILES "snpe_single_ssd.cc")
  list(APPEND SNPE_DIRS "${SNPE_INCLUDE_DIR}")
  add_library(SNPE SHARED IMPORTED)
  set_target_properties(SNPE PROPERTIES
    IMPORTED_LOCATION ${SNPE_LIB_DIR}/libSNPE.so
  )

  set(SNPE SNPE)
endif()

add_library(${GST_MLE_LIBRARY} SHARED
  ${SOURCE_FILES}
)

target_include_directories(${GST_MLE_LIBRARY} PUBLIC
  ${GST_INCLUDE_DIRS}
  ${SYSROOT_INCDIR}/ion_headers
  ${SNPE_DIRS}
)

target_include_directories(${GST_MLE_LIBRARY} PRIVATE
  ${KERNEL_BUILDDIR}/usr/include
  ${SYSROOT_INCDIR}/ion_headers
  ${SNPE_DIRS}
)

target_link_libraries(${GST_MLE_LIBRARY} PRIVATE
  log
  dl
  cutils
  jsoncpp
  fastcvopt
  tensorflow-lite
  ${SNPE}
)

install(
  TARGETS ${GST_MLE_LIBRARY}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              GROUP_EXECUTE GROUP_READ
)
