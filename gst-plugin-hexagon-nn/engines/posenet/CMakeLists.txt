set(POSENET Engine_POSENET)

add_library(${POSENET} SHARED
  posenetengine.cc
)

target_include_directories(${POSENET} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${POSENET} PUBLIC
  nnengine
)

target_link_libraries(${POSENET} PRIVATE
  log
)

install(
  TARGETS ${POSENET}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)


###################### model ########################

set(POSENET_NN posenet_nn)

add_library(${POSENET_NN} SHARED
  posenet_mobilenet_v1_075_quant_nn.c
)

target_link_libraries(${POSENET_NN} PUBLIC
  nndriver
)

target_compile_options(${POSENET_NN} PRIVATE -Wno-strict-aliasing)

install(
  TARGETS ${POSENET_NN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)
