cmake_minimum_required(VERSION 3.8.2)
project(GST_TEST_FRAMEWORK LANGUAGES C)

set(GST_TEST_FRAMEWORK gst-test-framework)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_CHECK
  REQUIRED gstreamer-check-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_VIDEO
  REQUIRED gstreamer-video-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(GST_PBUTILS
  REQUIRED gstreamer-pbutils-1.0>=${GST_VERSION_REQUIRED})

include_directories(utils)

add_executable(${GST_TEST_FRAMEWORK}
  main.c
  utils/suite-utils.c
  suite-camera/suite-camera-case.c
  suite-camera/suite-camera-pipeline.c
  suite-ML/suite-ml-case.c
  suite-ML/suite-ml-pipeline.c
)

target_include_directories(${GST_TEST_FRAMEWORK} PRIVATE
  ${GST_INCLUDE_DIRS}
  ${GST_CHECK_INCLUDE_DIRS}
)

target_link_libraries(${GST_TEST_FRAMEWORK} PRIVATE
  ${GST_LIBRARIES}
  ${GST_CHECK_LIBRARIES}
  ${GST_PBUTILS_LIBRARIES}
  ${GST_VIDEO_LIBRARIES}
  qtimlmeta
)

install(
  TARGETS ${GST_TEST_FRAMEWORK}
  RUNTIME DESTINATION ${GST_PLUGINS_QTI_OSS_INSTALL_BINDIR}
  PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
              GROUP_EXECUTE GROUP_READ
              WORLD_EXECUTE WORLD_READ
)
