set(DEEP_LAB_V3 Engine_DEEPLABV3)

add_library(${DEEP_LAB_V3} SHARED
  deeplabengine.cc
)

target_include_directories(${DEEP_LAB_V3} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${DEEP_LAB_V3} PUBLIC
  nnengine
)
 
target_link_libraries(${DEEP_LAB_V3} PRIVATE
  log
)

install(
  TARGETS ${DEEP_LAB_V3}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)


###################### model ########################

set(DEEP_LAB_V3_NN deeplabv3_nn)

add_library(${DEEP_LAB_V3_NN} SHARED
  deeplabv3_nn.c
)

target_link_libraries(${DEEP_LAB_V3_NN} PUBLIC
  nndriver
)

target_compile_options(${DEEP_LAB_V3_NN} PUBLIC -Wno-strict-aliasing)

install(
  TARGETS ${DEEP_LAB_V3_NN}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)