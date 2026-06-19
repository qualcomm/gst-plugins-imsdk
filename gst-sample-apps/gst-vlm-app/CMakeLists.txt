cmake_minimum_required(VERSION 3.16)
project(GST-VLM-APP LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_subdirectory(Genie)

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_BASE
  REQUIRED gstreamer-base-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})

pkg_check_modules(GST_WEBRTC
  REQUIRED gstreamer-webrtc-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_SDP
  REQUIRED gstreamer-sdp-1.0>=${GST_VERSION_REQUIRED})

pkg_check_modules(JSON_GLIB REQUIRED json-glib-1.0)

# Check for libsoup version
pkg_search_module(GST_SOUP QUIET libsoup-3.0)
if (GST_SOUP_FOUND)
  set(GST_SOUP3 TRUE)
  message(STATUS "Using libsoup-3.0")
else ()
  pkg_search_module(GST_SOUP QUIET libsoup-2.4)
  if (GST_SOUP_FOUND)
    set(GST_SOUP3 FALSE)
    message(STATUS "Using libsoup-2.4")
  else ()
    message(FATAL_ERROR "No libsoup modules found")
  endif()
endif()

if(GST_SOUP3)
  add_definitions(-DGST_SOUP3)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

set(GST_EXAMPLE_BIN standalone-genie-vlm-test)

add_executable(${GST_EXAMPLE_BIN}
  standalone-genie-vlm-test.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${JSON_GLIB_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
  Genie
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  genie-vlm
)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_EXAMPLE_BIN gst-vlm-multistream-usecase)

add_executable(${GST_EXAMPLE_BIN}
  vlm-multistream-usecase.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${JSON_GLIB_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
  Genie
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${JSON_GLIB_LIBRARIES}
  ${GST_WEBRTC_LIBRARIES}
  ${GST_SDP_LIBRARIES}
  genie-vlm
)

if(GST_SOUP3)
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-3.0
)
else()
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-2.4
)
endif()

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_EXAMPLE_BIN gst-vlm-rz-usecase)

add_executable(${GST_EXAMPLE_BIN}
  vlm-rz-usecase.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${JSON_GLIB_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
  Genie
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${JSON_GLIB_LIBRARIES}
  ${GST_WEBRTC_LIBRARIES}
  ${GST_SDP_LIBRARIES}
  genie-vlm
)

if(GST_SOUP3)
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-3.0
)
else()
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-2.4
)
endif()

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_EXAMPLE_BIN gst-vlm-multistream-usecase-ipc)

add_executable(${GST_EXAMPLE_BIN}
  vlm-multistream-usecase-ipc.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${JSON_GLIB_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
  Genie
  IPC
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${JSON_GLIB_LIBRARIES}
  ${GST_WEBRTC_LIBRARIES}
  ${GST_SDP_LIBRARIES}
  genie-vlm
)

if(GST_SOUP3)
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-3.0
)
else()
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-2.4
)
endif()

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)

set(GST_EXAMPLE_BIN gst-vlm-rz-usecase-ipc)

add_executable(${GST_EXAMPLE_BIN}
  vlm-rz-usecase-ipc.cc
)

target_include_directories(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${JSON_GLIB_INCLUDE_DIRS}
  ${GST_SOUP_INCLUDE_DIRS}
  Genie
  IPC
)

target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  ${GST_LIBRARIES}
  ${GST_BASE_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  ${JSON_GLIB_LIBRARIES}
  ${GST_WEBRTC_LIBRARIES}
  ${GST_SDP_LIBRARIES}
  genie-vlm
)

if(GST_SOUP3)
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-3.0
)
else()
target_link_libraries(${GST_EXAMPLE_BIN} PRIVATE
  soup-2.4
)
endif()

target_compile_definitions(gst-vlm-multistream-usecase-ipc PUBLIC GST_USE_UNSTABLE_API)
target_compile_definitions(gst-vlm-multistream-usecase PUBLIC GST_USE_UNSTABLE_API)
target_compile_definitions(gst-vlm-rz-usecase-ipc PUBLIC GST_USE_UNSTABLE_API)
target_compile_definitions(gst-vlm-rz-usecase PUBLIC GST_USE_UNSTABLE_API)

install(
  TARGETS ${GST_EXAMPLE_BIN}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
