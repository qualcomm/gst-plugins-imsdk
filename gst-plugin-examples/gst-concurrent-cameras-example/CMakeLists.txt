cmake_minimum_required(VERSION 3.8.2)

# GStreamer gst-concurrent-cameras-example
set(GST_EXAMPLE_BIN gst-concurrent-cameras-example)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
