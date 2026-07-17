cmake_minimum_required(VERSION 3.16)
project(gstreamer-qcom-oss-base-1.0
  VERSION ${GST_PLUGINS_QTI_OSS_VERSION}
  LANGUAGES C CXX
)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(PkgConfig)
find_package(Python3 3.12 COMPONENTS Interpreter Development)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})
pkg_check_modules(PYGOBJECT pygobject-3.0)

add_subdirectory(gi/overrides)
