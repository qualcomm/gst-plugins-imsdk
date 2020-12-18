set(MNET_SSD Engine_MNETSSD)

add_library(${MNET_SSD} SHARED
  mnetssdengine.cc
)

target_include_directories(${MNET_SSD} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${MNET_SSD} PUBLIC
  nnengine
)

target_link_libraries(${MNET_SSD} PRIVATE
  log
)

install(
  TARGETS ${MNET_SSD}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)

###################### model ########################

set(MNET_SSD_NN mssdv1_nn)

add_library(${MNET_SSD_NN} SHARED
  mssdv1_nn.c
)

target_link_libraries(${MNET_SSD_NN} PUBLIC
  nndriver
)

target_compile_options(${MNET_SSD_NN} PRIVATE -Wno-strict-aliasing)

install(
  TARGETS ${MNET_SSD_NN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)
