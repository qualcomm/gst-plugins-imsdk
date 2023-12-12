cmake_minimum_required(VERSION 3.8.2)
project(GST_SAMPLE_APPS LANGUAGES C CXX)

include_directories(${SYSROOT_INCDIR})
link_directories(${SYSROOT_LIBDIR})

find_package(PkgConfig)

# Get the pkgconfigs exported by the automake tools
pkg_check_modules(GST
  REQUIRED gstreamer-1.0>=${GST_VERSION_REQUIRED})

if (ENABLE_AUDIO)
  add_subdirectory(gst-audio-decode-example)
endif() # ENABLE_AUDIO
