# MQTT protocol adaptor based on mosquitto

set(MQTT gstqtimqttadaptor)

add_library(${MQTT} SHARED
  mqtt.c
)

target_include_directories(${MQTT} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${MQTT} PRIVATE
  ${GST_LIBRARIES}
  mosquitto
)

install(
  TARGETS ${MQTT}
  LIBRARY DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_READ OWNER_WRITE
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)