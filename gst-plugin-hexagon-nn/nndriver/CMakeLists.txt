set(NNDRIVER nndriver)

add_library(${NNDRIVER} SHARED
  nndriver.cc
  hexagon/hexagon_nn_stub.c
  hexagon/hexagon_nn_domains_stub.c
)

target_include_directories(${NNDRIVER} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/hexagon
  ${SYSROOT_INCDIR}/fastrpc/inc
)

target_link_libraries(${NNDRIVER} PUBLIC
  cdsprpc
)

install(
  TARGETS ${NNDRIVER}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)