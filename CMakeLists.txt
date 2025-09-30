# This CMake file is included solely to enable building the entire project as a single unit on Ubuntu.

cmake_minimum_required(VERSION 3.10)
project(gst-plugins-qti-oss LANGUAGES C CXX)

# Few plugins have been disabled for now due to unavailable dependent packages.
# === Core Plugins ===
#add_subdirectory(gst-plugin-qmmfsrc)
add_subdirectory(gst-plugin-vcomposer)
add_subdirectory(gst-plugin-tools)
add_subdirectory(gst-plugin-batch)
add_subdirectory(gst-plugin-metamux)
add_subdirectory(gst-plugin-socket)
add_subdirectory(gst-plugin-vsplit)
add_subdirectory(gst-plugin-vtransform)
add_subdirectory(gst-plugin-voverlay)
add_subdirectory(gst-plugin-restricted-zone)
add_subdirectory(gst-plugin-rtspbin)
add_subdirectory(gst-plugin-redissink)
add_subdirectory(gst-plugin-smartvencbin)
add_subdirectory(gst-plugin-base)
#add_subdirectory(gst-plugin-camreproc)
#add_subdirectory(gst-plugin-jpegenc)
add_subdirectory(gst-plugin-videotemplate)

# === ML Plugins ===
add_subdirectory(gst-plugin-mlaconverter)
add_subdirectory(gst-plugin-mlaclassification)
add_subdirectory(gst-plugin-mldemux)
add_subdirectory(gst-plugin-mlvconverter)
add_subdirectory(gst-plugin-mlvclassification)
add_subdirectory(gst-plugin-mlvsuperresolution)
add_subdirectory(gst-plugin-mlvdetection)
add_subdirectory(gst-plugin-mlvpose)
add_subdirectory(gst-plugin-mlvsegmentation)
add_subdirectory(gst-plugin-mltools)
#add_subdirectory(gst-plugin-mltflite)
#add_subdirectory(gst-plugin-mlsnpe)
#add_subdirectory(gst-plugin-mlqnn)
add_subdirectory(gst-plugin-mlmetaparser)
add_subdirectory(gst-plugin-metatransform)
add_subdirectory(gst-plugin-objtracker)
add_subdirectory(gst-plugin-mlmetaextractor)
add_subdirectory(gst-plugin-mlpostprocess)

# === Sample Apps and Test Framework ===
add_subdirectory(gst-sample-apps/gst-sample-apps-utils)
add_subdirectory(gst-sample-apps)
add_subdirectory(gst-test-framework)
add_subdirectory(gst-python-examples)

# === Messaging Plugins ===
add_subdirectory(gst-plugin-msgbroker)

#TODO: Disable based on pkgconfig/header file checks
if(DEFINED ENABLE_NON_QLI)
  add_subdirectory(gst-plugin-codec2)
  add_subdirectory(gst-plugin-cv-imgpyramid)
  add_subdirectory(gst-plugin-cv-optclflow)
  add_subdirectory(gst-plugin-dfs)
  add_subdirectory(gst-plugin-drmdecryptor)
  add_subdirectory(gst-plugin-mlaic)
  add_subdirectory(gst-plugin-examples)
endif()
