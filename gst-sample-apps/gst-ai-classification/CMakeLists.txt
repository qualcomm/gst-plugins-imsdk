cmake_minimum_required(VERSION 3.8.2)
project(GST-AI-CLASSIFICATION LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})
link_directories(${GST_PLUGINS_QTI_OSS_INSTALL_LIBDIR})

find_package(PkgConfig)

# Add config files
file(GLOB CONFIG_FILE *.json)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

pkg_check_modules(GST_JSON
  REQUIRED json-glib-1.0)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(GST_EXAMPLE_BIN gst-ai-classification)

# Add destination directory for config file
set(DEST_DIR /opt)

add_executable(${GST_EXAMPLE_BIN}
  main.c
)

# Add target specific precompiler defintions.
target_compile_definitions(${GST_EXAMPLE_BIN} PRIVATE
  $<$<BOOL:${ENABLE_CAMERA}>:ENABLE_CAMERA>
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${GST_JSON_INCLUDE_DIRS}
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_JSON_LIBRARIES}
  gstappsutils
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

install(FILES ${CONFIG_FILE} DESTINATION ${DEST_DIR})
