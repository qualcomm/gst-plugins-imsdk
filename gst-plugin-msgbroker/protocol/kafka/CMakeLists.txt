# KAFKA protocol adaptor based on rdkafka

set(KAFKA gstqtikafkaadaptor)

add_library(${KAFKA} SHARED
  kafka.c
)

pkg_check_modules(RDKAFKA REQUIRED rdkafka)

set_target_properties(${KAFKA} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${KAFKA} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${RDKAFKA_INCLUDE_DIRS}
)

target_link_libraries(${KAFKA} PRIVATE
  qtimsgadaptor_interface
  ${GST_LIBRARIES}
  ${CMAKE_DL_LIBS}
  ${RDKAFKA_LIBRARIES}
)

install(
  TARGETS ${KAFKA}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_READ OWNER_WRITE
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
