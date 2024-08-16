set(GST_EXAMPLE_BIN gst-ai-pose-detection)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${ENABLE_CAMERA}>:ENABLE_CAMERA>
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
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
