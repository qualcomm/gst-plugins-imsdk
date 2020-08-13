set(NNENGINE nnengine)

add_library(${NNENGINE} SHARED
  nnengine.cc
)

target_include_directories(${NNENGINE} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${NNENGINE} PUBLIC
  nndriver
)

target_link_libraries(${NNENGINE} PRIVATE
  fastcvopt
)

target_compile_definitions(${NNENGINE} PRIVATE
  NNENGINE_LIB_DIR="${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}"
)

install(
  TARGETS ${NNENGINE}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
)
