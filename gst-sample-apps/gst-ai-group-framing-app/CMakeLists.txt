cmake_minimum_required(VERSION 3.16)

project(GST-AI-GROUP-FRAMING-APP LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig REQUIRED)
# Get the pkgconfigs exported by the automake tools

pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
# GStreamer AI group framing application.

set(GST_AI_GROUP_FRAMING_APP gst-ai-group-framing-app)

add_executable(${GST_AI_GROUP_FRAMING_APP}
  main.c
)

target_include_directories(${GST_AI_GROUP_FRAMING_APP} PRIVATE
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(${GST_AI_GROUP_FRAMING_APP} PRIVATE
  ${GST_LIBRARIES}
)

install(
  TARGETS ${GST_AI_GROUP_FRAMING_APP}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
