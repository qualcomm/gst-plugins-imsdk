# MQTT protocol adaptor based on mosquitto

set(MQTT gstqtimqttadaptor)

add_library(${MQTT} SHARED
  mqtt.c
)

set_target_properties(${MQTT} PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${MQTT} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${MQTT} PRIVATE
  qtimsgadaptor_interface
  ${GST_LIBRARIES}
  ${CMAKE_DL_LIBS}
)

install(
  TARGETS ${MQTT}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_READ OWNER_WRITE
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
